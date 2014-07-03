#include "tcpclient.h"
#include "buffer.h"
#include "log.h"

#include <sys/types.h>
#include <sys/socket.h>
#include <netdb.h>
#include <errno.h>
#include <fcntl.h>

#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <time.h>

#include <glib.h>
#include <ev.h>


int tcpclient_default_callback(void *tc, enum tcpclient_event event, char *data, size_t len) {
	// default is to do nothing
	if(event == EVENT_RECV) {
		free(data);
	}
	return 0;
}

void tcpclient_set_state(tcpclient_t *client, enum tcpclient_state state) {
	stats_log("tcpclient: State transition %s -> %s",
			tcpclient_state_name[client->state],
			tcpclient_state_name[state]);
	client->state = state;
}

void tcpclient_connect_timeout(struct ev_loop *loop, struct ev_timer *watcher, int events) {
	tcpclient_t *client = (tcpclient_t *)watcher->data;
	ev_io_stop(loop, &client->connect_watcher);

	stats_log("tcpclient: Connection timeout");
	client->last_error = time(NULL);
	tcpclient_set_state(client, STATE_BACKOFF);
	client->callback_error(client, EVENT_ERROR, NULL, 0);
}

int tcpclient_init(tcpclient_t *client, struct ev_loop *loop) {
	client->state = STATE_INIT;
	client->loop = loop;
	client->sd = -1;
	client->addr = NULL;

	client->callback_connect = &tcpclient_default_callback;
	client->callback_sent = &tcpclient_default_callback;
	client->callback_recv = &tcpclient_default_callback;
	client->callback_error = &tcpclient_default_callback;
	buffer_init(&client->send_queue);
	ev_timer_init(&client->timeout_watcher, tcpclient_connect_timeout, TCPCLIENT_CONNECT_TIMEOUT, 0);

	return 0;
}

void tcpclient_set_connect_callback(tcpclient_t *client, tcpclient_callback *callback) {
	client->callback_connect = *callback;
}

void tcpclient_set_sent_callback(tcpclient_t *client, tcpclient_callback *callback) {
	client->callback_sent = *callback;
}

void tcpclient_set_recv_callback(tcpclient_t *client, tcpclient_callback *callback) {
	client->callback_recv = *callback;
}

void tcpclient_set_error_callback(tcpclient_t *client, tcpclient_callback *callback) {
	client->callback_error = *callback;
}

void tcpclient_read_event(struct ev_loop *loop, struct ev_io *watcher, int events) {
	tcpclient_t *client = (tcpclient_t *)watcher->data;
	ssize_t len;
	char *buf;

	if(!(events & EV_READ)) {
		return;
	}

	buf = malloc(TCPCLIENT_RECV_BUFFER);
	len = recv(client->sd, buf, TCPCLIENT_RECV_BUFFER, 0);
	if(len < 0) {
		stats_log("tcpclient: Error from recv: %s", strerror(errno));
		ev_io_stop(client->loop, &client->read_watcher);
		ev_io_stop(client->loop, &client->write_watcher);
		close(client->sd);
		free(buf);
		tcpclient_set_state(client, STATE_BACKOFF);
		client->last_error = time(NULL);
		client->callback_error(client, EVENT_ERROR, NULL, 0);
		return;
	}

	if(len == 0) {
		stats_log("tcpclient: Server closed connection");
		ev_io_stop(client->loop, &client->read_watcher);
		ev_io_stop(client->loop, &client->write_watcher);
		close(client->sd);
		free(buf);
		tcpclient_set_state(client, STATE_BACKOFF);
		client->last_error = time(NULL);
		client->callback_error(client, EVENT_ERROR, NULL, 0);
		return;
	}
	client->callback_recv(client, EVENT_RECV, buf, len);
}


void tcpclient_write_event(struct ev_loop *loop, struct ev_io *watcher, int events) {
	tcpclient_t *client = (tcpclient_t *)watcher->data;
	ssize_t len;

	if(!(events & EV_WRITE)) {
		return;
	}

	if(buffer_datacount(&client->send_queue) > 0) {
		len = send(client->sd, buffer_head(&client->send_queue), buffer_datacount(&client->send_queue), 0);
		if(len < 0) {
			stats_log("tcpclient: Error from send: %s", strerror(errno));
			ev_io_stop(client->loop, &client->write_watcher);
			ev_io_stop(client->loop, &client->read_watcher);
			client->last_error = time(NULL);
			tcpclient_set_state(client, STATE_BACKOFF);
			close(client->sd);
			client->callback_error(client, EVENT_ERROR, NULL, 0);
			return;
		}

		client->callback_sent(client, EVENT_SENT, (char *)buffer_head(&client->send_queue), (size_t)len);
		buffer_consume(&client->send_queue, len);
		buffer_realign(&client->send_queue);
	}else{
		ev_io_stop(client->loop, &client->write_watcher);
	}
}

void tcpclient_connected(struct ev_loop *loop, struct ev_io *watcher, int events) {
	tcpclient_t *client = (tcpclient_t *)watcher->data;
	int err;
	socklen_t len = sizeof(err);

	// Cancel timeout timer
	ev_timer_stop(loop, &client->timeout_watcher);
	ev_io_stop(loop, &client->connect_watcher);

	getsockopt(client->sd, SOL_SOCKET, SO_ERROR, &err, &len);

	if((events & EV_ERROR) || err) {
		stats_log("tcpclient: Connect failed");
		close(client->sd);
		client->last_error = time(NULL);
		tcpclient_set_state(client, STATE_BACKOFF);
		return;
	}

	tcpclient_set_state(client, STATE_CONNECTED);

	// Setup events for recv
	client->read_watcher.data = client;
	client->write_watcher.data = client;
	ev_io_init(&client->read_watcher, tcpclient_read_event, client->sd, EV_READ);
	ev_io_init(&client->write_watcher, tcpclient_write_event, client->sd, EV_WRITE);
	ev_io_start(client->loop, &client->read_watcher);
	ev_io_start(client->loop, &client->write_watcher);

	client->callback_connect(client, EVENT_CONNECTED, NULL, 0);
}

int tcpclient_connect(tcpclient_t *client, char *host, char *port) {
	struct addrinfo hints;
	struct addrinfo *addr;
	int sd;

	if(client->state == STATE_CONNECTED || client->state == STATE_CONNECTING) {
		// Already connected, do nothing
		return 1;
	}

	if(client->state == STATE_BACKOFF) {
		// If backoff timer has expired, change to STATE_INIT and call recursively
		if( (time(NULL) - client->last_error) > TCPCLIENT_RETRY_TIMEOUT ) {
			tcpclient_set_state(client, STATE_INIT);
			return tcpclient_connect(client, host, port);
		}else{
			return 2;
		}
	}

	if(client->state == STATE_INIT) {
		// Resolve address, create socket, set nonblocking, setup callbacks, fire connect
		if(client->addr == NULL) {
			memset(&hints, 0, sizeof(hints));
			hints.ai_family = AF_UNSPEC;
			hints.ai_socktype = SOCK_STREAM;
			hints.ai_flags = AI_PASSIVE;
			if(getaddrinfo(host, port, &hints, &addr) != 0) {
				stats_log("tcpclient: Error resolving backend address %s: %s", host, gai_strerror(errno));
				client->last_error = time(NULL);
				tcpclient_set_state(client, STATE_BACKOFF);
				client->callback_error(client, EVENT_ERROR, NULL, 0);
				return 3;
			}
			client->addr = addr;
		}else{
			addr = client->addr;
		}

		if((sd = socket(addr->ai_family, addr->ai_socktype, addr->ai_protocol)) < 0) {
			stats_log("tcpclient: Unable to create socket: %s", strerror(errno));
			client->last_error = time(NULL);
			tcpclient_set_state(client, STATE_BACKOFF);
			client->callback_error(client, EVENT_ERROR, NULL, 0);
			return 4;
		}
		client->sd = sd;

		if(fcntl(sd, F_SETFL, (fcntl(sd, F_GETFL) | O_NONBLOCK)) != 0) {
			stats_log("tcpclient: Unable to set socket to non-blocking: %s" , strerror(errno));
			client->last_error = time(NULL);
			tcpclient_set_state(client, STATE_BACKOFF);
			close(sd);
			client->callback_error(client, EVENT_ERROR, NULL, 0);
			return 5;
		}

		client->connect_watcher.data = client;
		client->timeout_watcher.data = client;
		ev_io_init(&client->connect_watcher, tcpclient_connected, sd, EV_WRITE);
		ev_io_start(client->loop, &client->connect_watcher);
		ev_timer_set(&client->timeout_watcher, TCPCLIENT_CONNECT_TIMEOUT, 0);
		ev_timer_start(client->loop, &client->timeout_watcher);

		if(connect(sd, addr->ai_addr, addr->ai_addrlen) != 0 && errno != EINPROGRESS) {
			stats_log("stats: Unable to connect: %s", strerror(errno));
			client->last_error = time(NULL);
			tcpclient_set_state(client, STATE_BACKOFF);
			ev_timer_stop(client->loop, &client->timeout_watcher);
			ev_io_stop(client->loop, &client->connect_watcher);
			close(sd);
			client->callback_error(client, EVENT_ERROR, NULL, 0);
			return 6;
		}

		tcpclient_set_state(client, STATE_CONNECTING);
		return 0;
	}

	stats_log("tcpclient: Connect with unknown state %i", client->state);
	return 7;
}

int tcpclient_sendall(tcpclient_t *client, char *buf, size_t len) {
	if(client->addr == NULL) {
		stats_log("tcpclient: Cannot send before connect!");
		return 1;
	}else{
		// Does nothing if we're already connected, triggers a reconnect if backoff
		// has expired.
		tcpclient_connect(client, NULL, NULL);
	}

	if(buffer_datacount(&client->send_queue) > TCPCLIENT_SEND_QUEUE) {
		stats_log("tcpclient: Send queue is full, dropping data");
		return 2;
	}

	while(buffer_spacecount(&client->send_queue) < len) {
		buffer_expand(&client->send_queue);
	}
	memcpy(buffer_tail(&client->send_queue), buf, len);
	buffer_produced(&client->send_queue, len);

	if(client->state == STATE_CONNECTED) {
		ev_io_start(client->loop, &client->write_watcher);
	}
	return 0;
}

void tcpclient_destroy(tcpclient_t *client, int drop_queue) {
	buffer_destroy(&client->send_queue);
	if(client->addr != NULL) {
		freeaddrinfo(client->addr);
	}
}
