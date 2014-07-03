#include "tcpclient.h"
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


enum tcpclient_state {
	STATE_INIT = 0,
	STATE_CONNECTING,
	STATE_BACKOFF,
	STATE_CONNECTED,
	STATE_TERMINATED
};

struct tcpclient_t {
	tcpclient_callback callback_connect;
	tcpclient_callback callback_sent;
	tcpclient_callback callback_recv;
	tcpclient_callback callback_error;

	struct ev_loop *loop;
	ev_timer timeout_watcher;
	ev_io connect_watcher;
	ev_io recv_watcher;

	GQueue *send_queue;
	enum tcpclient_state state;
	time_t last_error;
	int retry_count;
	int sd;
};


int tcpclient_init(tcpclient_t *client, struct ev_loop *loop) {
	client->state = STATE_INIT;
	client->loop = loop;
	client->sd = -1;
	client->callback_connect = NULL;
	client->callback_sent = NULL;
	client->callback_recv = NULL;
	client->callback_error = NULL;

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

void tcpclient_recv(struct ev_loop *loop, struct ev_io *watcher, int events) {
	tcpclient_t *client = (tcpclient_t *)watcher->data;
	ssize_t len;
	char *buf;

	buf = malloc(TCPCLIENT_RECV_BUFFER);
	len = recv(client->sd, buf, TCPCLIENT_RECV_BUFFER, 0);
	if(len < 0) {
		stats_log("tcpclient: Error from recv: %s", strerror(errno));
		ev_io_stop(client->loop, &client->recv_watcher);
		close(client->sd);
		client->state = STATE_BACKOFF;
		client->last_error = time(NULL);
		client->callback_error(client, EVENT_ERROR, NULL, 0);
	}
	client->callback_recv(client, EVENT_RECV, buf, len);
}

void tcpclient_connected(struct ev_loop *loop, struct ev_io *watcher, int events) {
	tcpclient_t *client = (tcpclient_t *)watcher->data;
	// Cancel timeout timer
	ev_timer_stop(loop, &client->timeout_watcher);

	client->state = STATE_CONNECTED;

	// Setup events for recv
	client->recv_watcher.data = client;
	ev_io_init(&client->recv_watcher, tcpclient_recv, client->sd, EV_READ);
	ev_io_start(client->loop, &client->recv_watcher);

	client->callback_connect(client, EVENT_CONNECTED, NULL, 0);
}

void tcpclient_connect_timeout(struct ev_loop *loop, struct ev_timer *watcher, int events) {
	tcpclient_t *client = (tcpclient_t *)watcher->data;
	ev_io_stop(loop, &client->connect_watcher);
	client->last_error = time(NULL);
	client->state = STATE_BACKOFF;
	client->callback_error(client, EVENT_ERROR, NULL, 0);
}

int tcpclient_connect(tcpclient_t *client, char *host, char *port) {
	struct addrinfo hints;
	struct addrinfo *addr;
	int sd;

	if(client->state == STATE_CONNECTED) {
		// Already connected, do nothing
		return 1;
	}

	if(client->state == STATE_BACKOFF) {
		// If backoff timer has expired, change to STATE_INIT and call recursively
		if( (time(NULL) - client->last_error) > TCPCLIENT_RETRY_TIMEOUT ) {
			client->state = STATE_INIT;
			return tcpclient_connect(client, host, port);
		}else{
			return 2;
		}
	}

	if(client->state == STATE_INIT) {
		// Resolve address, create socket, set nonblocking, setup callbacks, fire connect
		memset(&hints, 0, sizeof(hints));
		hints.ai_family = AF_UNSPEC;
		hints.ai_socktype = SOCK_STREAM;
		hints.ai_flags = AI_PASSIVE;
		if(getaddrinfo(host, port, &hints, &addr) != 0) {
			stats_log("tcpclient: Error resolving backend address %s: %s", host, gai_strerror(errno));
			client->last_error = time(NULL);
			client->state = STATE_BACKOFF;
			client->callback_error(client, EVENT_ERROR, NULL, 0);
			return 3;
		}

		if((sd = socket(addr->ai_family, addr->ai_socktype, addr->ai_protocol)) == -1) {
			stats_log("tcpclient: Unable to create socket: %s", strerror(errno));
			client->last_error = time(NULL);
			client->state = STATE_BACKOFF;
			freeaddrinfo(addr);
			client->callback_error(client, EVENT_ERROR, NULL, 0);
			return 4;
		}

		if(fcntl(sd, F_SETFL, (fcntl(sd, F_GETFL) | O_NONBLOCK)) != 0) {
			stats_log("tcpclient: Unable to set socket to non-blocking: %s" , strerror(errno));
			client->last_error = time(NULL);
			client->state = STATE_BACKOFF;
			freeaddrinfo(addr);
			close(sd);
			client->callback_error(client, EVENT_ERROR, NULL, 0);
			return 5;
		}

		client->connect_watcher.data = client;
		client->timeout_watcher.data = client;
		ev_io_init(&client->connect_watcher, tcpclient_connected, sd, EV_WRITE);
		ev_io_start(client->loop, &client->connect_watcher);
		ev_timer_init(&client->timeout_watcher, tcpclient_connect_timeout, TCPCLIENT_CONNECT_TIMEOUT, 0);
		ev_timer_start(client->loop, &client->timeout_watcher);

		if(connect(sd, addr->ai_addr, addr->ai_addrlen) != 0 && errno != EINPROGRESS) {
			stats_log("stats: Unable to connect to %s: %s", host, strerror(errno));
			client->last_error = time(NULL);
			client->state = STATE_BACKOFF;
			freeaddrinfo(addr);
			close(sd);
			client->callback_error(client, EVENT_ERROR, NULL, 0);
			return 6;
		}

		freeaddrinfo(addr);
		client->state = STATE_CONNECTING;
		client->sd = sd;
		stats_log("tcpclient: Connecting to %s", host);

		if(errno != EINPROGRESS) {
			// Connect finished immediately, we're good
			client->state = STATE_CONNECTED;
			client->callback_connect(client, EVENT_CONNECTED, NULL, 0);
		}
		return 0;
	}

	stats_log("tcpclient: Connect with unknown state %i", client->state);
	return 7;
}

int tcpclient_sendall(tcpclient_t *client, char *buf, size_t len) {
	ssize_t bytes_sent;
	char *p;

	if(client->state == STATE_CONNECTING) {
		// The connection will probably open soon, queue up the request until connect
		// finishes or errors
		return 1;
	}

	if(client->state == STATE_CONNECTED) {
		// Just send immediately
		bytes_sent = send(client->sd, buf, len, 0);
		if(bytes_sent < 0) {
			// There was an error, close the socket immediately, don't wait for FIN
			stats_log("tcpclient: Error sending to socket: %s", strerror(errno));
			client->state = STATE_BACKOFF;
			close(client->sd);
			client->callback_error(client, EVENT_ERROR, NULL, 0);
			return 2;
		}

		if(bytes_sent == 0) {
			stats_log("tcpclient: Server closed connection");
			client->state = STATE_BACKOFF;
			close(client->sd);
			client->callback_error(client, EVENT_ERROR, NULL, 0);
			return 3;
		}

		if(bytes_sent < len) {
			// Not all data was sent, offset by bytes_sent and call recursively
			p = buf + bytes_sent;
			len -= bytes_sent;
			return tcpclient_sendall(client, p, len);
		}

		client->callback_sent(client, EVENT_SENT, buf, len);
		return 0;
	}

	stats_log("tcpclient: sendall with unhandled state %i", client->state);
	return 100;
}
