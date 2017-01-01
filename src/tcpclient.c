#include "tcpclient.h"
#include "buffer.h"
#include "log.h"

#include <errno.h>
#include <fcntl.h>
#include <inttypes.h>
#include <netdb.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <time.h>
#include <unistd.h>

#include <ev.h>

#define DEFAULT_BUFFER_SIZE (1<<16)

static const char *tcpclient_state_name[] = {
	"INIT", "CONNECTING", "BACKOFF", "CONNECTED", "TERMINATED"
};

static int tcpclient_default_callback(void *tc, enum tcpclient_event event, void *context, char *data, size_t len) {
	// default is to do nothing
	if (event == EVENT_RECV) {
		free(data);
	}
	return 0;
}

static void tcpclient_set_state(tcpclient_t *client, enum tcpclient_state state) {
	stats_log("tcpclient[%s]: State transition %s -> %s",
			client->name,
			tcpclient_state_name[client->state],
			tcpclient_state_name[state]);
	client->state = state;
}

static void tcpclient_connect_timeout(struct ev_loop *loop, struct ev_timer *watcher, int events) {
	tcpclient_t *client = (tcpclient_t *)watcher->data;
	if (client->connect_watcher.started) {
		ev_io_stop(loop, &client->connect_watcher.watcher);
		client->connect_watcher.started = false;
	}

	close(client->sd);
	stats_error_log("tcpclient[%s]: Connection timeout", client->name);
	client->last_error = time(NULL);
	tcpclient_set_state(client, STATE_BACKOFF);
	client->callback_error(client, EVENT_ERROR, client->callback_context, NULL, 0);
}

int tcpclient_init(tcpclient_t *client,
		   struct ev_loop *loop,
		   void *callback_context,
		   struct proto_config *config,
		   char *host,
		   char *port,
		   char *protocol) {
	size_t len;

	client->state = STATE_INIT;
	client->loop = loop;
	client->sd = -1;
	client->addr = NULL;
	client->last_error = 0;
	client->failing = 0;
	client->config = config;
	client->socktype = SOCK_DGRAM;

	if(host == NULL) {
		stats_error_log("tcpclient_init: host is NULL\n");
		return 1;
	}
	len = strlen(host);
	if((client->host = calloc(1, len + 1)) == NULL) {
		stats_error_log("tcpclient[%s]: unable to allocate memory for host\n", client->host);
		return 1;
	}
	strncpy(client->host, host, len);

	if(port == NULL) {
		stats_error_log("tcpclient_init: port is NULL\n");
		free(client->host);
		return 1;
	}
	len = strlen(port);
	if((client->port = calloc(1, len + 1)) == NULL) {
		stats_error_log("tcpclient[%s]: unable to allocate memory for port\n", client->host);
		free(client->host);
		return 1;
	}
	strncpy(client->port, port, len);

	if(protocol == NULL) {
		// Default to TCP if protocol is not set
		if((client->protocol = calloc(1, 4)) == NULL) {
			stats_error_log("tcpclient[%s]: unable to allocate memory for protocol\n", client->host);
			free(client->host);
			free(client->port);
			return 1;
		}
		strncpy(client->protocol, "tcp", 3);
	}else{
		len = strlen(protocol);
		if((client->protocol = calloc(1, len + 1)) == NULL) {
			stats_error_log("tcpclient[%s]: unable to allocate memory for protocol\n", client->host);
			free(client->host);
			free(client->port);
			return 1;
		}
		strncpy(client->protocol, protocol, len);
	}

	strncpy(client->name, "UNRESOLVED", TCPCLIENT_NAME_LEN);

	client->callback_connect = &tcpclient_default_callback;
	client->callback_sent = &tcpclient_default_callback;
	client->callback_recv = &tcpclient_default_callback;
	client->callback_error = &tcpclient_default_callback;
	client->callback_context = callback_context;
	buffer_init(&client->send_queue);
	buffer_newsize(&client->send_queue, DEFAULT_BUFFER_SIZE);
	ev_timer_init(&client->timeout_watcher,
		      tcpclient_connect_timeout,
		      TCPCLIENT_CONNECT_TIMEOUT,
		      0);

	client->connect_watcher.started = false;
	client->read_watcher.started = false;
	client->write_watcher.started = false;
	return 0;
}

void tcpclient_set_sent_callback(tcpclient_t *client, tcpclient_callback callback) {
	client->callback_sent = callback;
}

static void tcpclient_read_event(struct ev_loop *loop, struct ev_io *watcher, int events) {
	tcpclient_t *client = (tcpclient_t *)watcher->data;
	ssize_t len;
	char *buf;

	if (!(events & EV_READ)) {
		return;
	}

	buf = malloc(TCPCLIENT_RECV_BUFFER);
	if (buf == NULL) {
		stats_error_log("tcpclient[%s]: Unable to allocate memory for receive buffer", client->name);
		return;
	}
	len = recv(client->sd, buf, TCPCLIENT_RECV_BUFFER, 0);
	if (len < 0) {
		stats_error_log("tcpclient[%s]: Error from recv: %s", client->name, strerror(errno));
		if (client->read_watcher.started) {
			ev_io_stop(client->loop, &client->read_watcher.watcher);
			client->read_watcher.started = false;
		}
		if (client->write_watcher.started) {
			ev_io_stop(client->loop, &client->write_watcher.watcher);
			client->write_watcher.started = false;
		}
		close(client->sd);
		free(buf);
		tcpclient_set_state(client, STATE_BACKOFF);
		client->last_error = time(NULL);
		client->callback_error(client, EVENT_ERROR, client->callback_context, NULL, 0);
		return;
	}

	if (len == 0) {
		stats_error_log("tcpclient[%s]: Server closed connection", client->name);
		ev_io_stop(client->loop, &client->read_watcher.watcher);
		ev_io_stop(client->loop, &client->write_watcher.watcher);
		close(client->sd);
		free(buf);
		tcpclient_set_state(client, STATE_INIT);
		client->last_error = time(NULL);
		client->callback_error(client, EVENT_ERROR, client->callback_context, NULL, 0);
		return;
	}
	client->callback_recv(client, EVENT_RECV, client->callback_context, buf, len);

}


static void tcpclient_write_event(struct ev_loop *loop, struct ev_io *watcher, int events) {
	tcpclient_t *client = (tcpclient_t *)watcher->data;
	buffer_t *sendq;

	if (!(events & EV_WRITE)) {
		return;
	}

	sendq = &client->send_queue;
	ssize_t buf_len = buffer_datacount(sendq);
	if (buf_len > 0) {
		ssize_t send_len = send(client->sd, sendq->head, buf_len, 0);
		stats_debug_log("tcpclient: sent %zd of %zd bytes to backend client %s via fd %d",
				send_len, buf_len, client->name, client->sd);
		if (send_len < 0) {
			stats_error_log("tcpclient[%s]: Error from send: %s", client->name, strerror(errno));
			ev_io_stop(client->loop, &client->write_watcher.watcher);
			ev_io_stop(client->loop, &client->read_watcher.watcher);
			client->last_error = time(NULL);
			tcpclient_set_state(client, STATE_BACKOFF);
			close(client->sd);
			client->callback_error(client, EVENT_ERROR, client->callback_context, NULL, 0);
			return;
		} else {
			client->callback_sent(client, EVENT_SENT, client->callback_context, sendq->head, (size_t) send_len);
			if (buffer_consume(sendq, send_len) != 0) {
				stats_error_log("tcpclient[%s]: Unable to consume send queue", client->name);
				return;
			}
			size_t qsize = buffer_datacount(&client->send_queue);
			if (client->failing && qsize < client->config->max_send_queue) {
				stats_log("tcpclient[%s]: client recovered from full queue, send queue is now %zd bytes",
					  client->name,
					  qsize);
				client->failing = 0;
			}
			if (qsize == 0) {
				ev_io_stop(client->loop, &client->write_watcher.watcher);
				client->write_watcher.started = false;
			}
		}
	} else {
		// No data left in the client's buffer, stop waiting
		// for write events.
		ev_io_stop(client->loop, &client->write_watcher.watcher);
		client->write_watcher.started = false;
	}
}

static void tcpclient_connected(struct ev_loop *loop, struct ev_io *watcher, int events) {
	tcpclient_t *client = (tcpclient_t *)watcher->data;
	int err;
	socklen_t len = sizeof(err);

	// Cancel timeout timer
	ev_timer_stop(loop, &client->timeout_watcher);
	ev_io_stop(loop, &client->connect_watcher.watcher);

	if (getsockopt(client->sd, SOL_SOCKET, SO_ERROR, &err, &len) != 0) {
		stats_error_log("tcpclient[%s]: Unable to get socket error state: %s", client->name, strerror(errno));
		return;
	}

	if ((events & EV_ERROR) || err) {
		stats_error_log("tcpclient[%s]: Connect failed: %s", client->name, strerror(err));
		close(client->sd);
		client->last_error = time(NULL);
		tcpclient_set_state(client, STATE_BACKOFF);
		return;
	}

	tcpclient_set_state(client, STATE_CONNECTED);

	// Setup events for recv
	client->read_watcher.started = true;
	client->read_watcher.watcher.data = client;
	ev_io_init(&client->read_watcher.watcher, tcpclient_read_event, client->sd, EV_READ);
	ev_io_start(client->loop, &client->read_watcher.watcher);

	client->write_watcher.started = true;
	client->write_watcher.watcher.data = client;
	ev_io_init(&client->write_watcher.watcher, tcpclient_write_event, client->sd, EV_WRITE);
	ev_io_start(client->loop, &client->write_watcher.watcher);

	client->callback_connect(client, EVENT_CONNECTED, client->callback_context, NULL, 0);
}

int tcpclient_connect(tcpclient_t *client) {
	struct addrinfo hints;
	struct addrinfo *addr;
	int sd;

	if (client->state == STATE_CONNECTED || client->state == STATE_CONNECTING) {
		// Already connected, do nothing
		return 1;
	}

	if (client->state == STATE_BACKOFF) {
		// If backoff timer has expired, change to STATE_INIT and call recursively
		if ((time(NULL) - client->last_error) > TCPCLIENT_RETRY_TIMEOUT) {
			tcpclient_set_state(client, STATE_INIT);
			return tcpclient_connect(client);
		} else {
			return 2;
		}
	}

	if (client->state == STATE_INIT) {
		// Resolve address, create socket, set nonblocking, setup callbacks, fire connect
		if (client->config->always_resolve_dns == true && client->addr != NULL) {
			freeaddrinfo(client->addr);
			client->addr = NULL;
		}

		if (client->addr == NULL) {
			// We only know about tcp and udp, so if we get something unexpected just
			// default to tcp
			if (strncmp(client->protocol, "udp", 3) == 0) {
				client->socktype = SOCK_DGRAM;
			} else {
				client->socktype = SOCK_STREAM;
			}
			memset(&hints, 0, sizeof(hints));
			hints.ai_family = AF_UNSPEC;
			hints.ai_socktype = client->socktype;
			hints.ai_flags = AI_PASSIVE;

			if (getaddrinfo(client->host, client->port, &hints, &addr) != 0) {
				stats_error_log("tcpclient: Error resolving backend address %s: %s", client->host, gai_strerror(errno));
				client->last_error = time(NULL);
				tcpclient_set_state(client, STATE_BACKOFF);
				client->callback_error(client, EVENT_ERROR, client->callback_context, NULL, 0);
				return 3;
			}
			client->addr = addr;
			snprintf(client->name, TCPCLIENT_NAME_LEN, "%s/%s/%s", client->host, client->port, client->protocol);
		} else {
			addr = client->addr;
		}

		if ((sd = socket(addr->ai_family, addr->ai_socktype, addr->ai_protocol)) < 0) {
			stats_error_log("tcpclient[%s]: Unable to create socket: %s", client->name, strerror(errno));
			client->last_error = time(NULL);
			tcpclient_set_state(client, STATE_BACKOFF);
			client->callback_error(client, EVENT_ERROR, client->callback_context, NULL, 0);
			return 4;
		}
#ifdef TCP_CORK
		if (client->config->enable_tcp_cork &&
		    addr->ai_family == AF_INET &&
		    addr->ai_socktype == SOCK_STREAM &&
		    addr->ai_protocol == IPPROTO_TCP) {
			int state = 1;
			if (setsockopt(sd, IPPROTO_TCP, TCP_CORK, &state, sizeof(state))) {
				stats_error_log("failed to set TCP_CORK");
			}
		}
#endif
		client->sd = sd;

		if (fcntl(sd, F_SETFL, (fcntl(sd, F_GETFL) | O_NONBLOCK)) != 0) {
			stats_error_log("tcpclient[%s]: Unable to set socket to non-blocking: %s", client->name, strerror(errno));
			client->last_error = time(NULL);
			tcpclient_set_state(client, STATE_BACKOFF);
			close(sd);
			client->callback_error(client, EVENT_ERROR, client->callback_context, NULL, 0);
			return 5;
		}

		client->connect_watcher.started = true;
		client->connect_watcher.watcher.data = client;
		client->timeout_watcher.data = client;
		ev_io_init(&client->connect_watcher.watcher, tcpclient_connected, sd, EV_WRITE);
		ev_io_start(client->loop, &client->connect_watcher.watcher);
		ev_timer_set(&client->timeout_watcher, TCPCLIENT_CONNECT_TIMEOUT, 0);
		ev_timer_start(client->loop, &client->timeout_watcher);

		if (connect(sd, addr->ai_addr, addr->ai_addrlen) != 0 && errno != EINPROGRESS) {
			stats_error_log("tcpclient[%s]: Unable to connect: %s", client->name, strerror(errno));
			client->last_error = time(NULL);
			tcpclient_set_state(client, STATE_BACKOFF);
			ev_timer_stop(client->loop, &client->timeout_watcher);
			ev_io_stop(client->loop, &client->connect_watcher.watcher);
			close(sd);
			client->callback_error(client, EVENT_ERROR, client->callback_context, NULL, 0);
			return 6;
		}

		tcpclient_set_state(client, STATE_CONNECTING);
		return 0;
	}

	stats_error_log("tcpclient[%s]: Connect with unknown state %i", client->name, client->state);
	return 7;
}

int tcpclient_sendall(tcpclient_t *client, const char *buf, size_t len) {
	buffer_t *sendq = &client->send_queue;

	if (client->addr == NULL) {
		stats_error_log("tcpclient[%s]: Cannot send before connect!", client->name);
		return 1;
	} else {
		// Does nothing if we're already connected, triggers a
		// reconnect if backoff has expired.
		tcpclient_connect(client);
	}

	if (buffer_datacount(&client->send_queue) >= client->config->max_send_queue) {
		if (client->failing == 0) {
			stats_error_log("tcpclient[%s]: send queue for %s client is full (at %zd bytes, max is %" PRIu64 " bytes), dropping data",
					client->name,
					tcpclient_state_name[client->state],
					buffer_datacount(&client->send_queue),
					client->config->max_send_queue);
			client->failing = 1;
		}
		return 2;
	}
	if (buffer_spacecount(sendq) < len) {
		if (buffer_realign(sendq) != 0) {
			stats_error_log("tcpclient[%s]: Unable to realign send queue", client->name);
			return 3;
		}
	}
	while (buffer_spacecount(sendq) < len) {
		if (buffer_expand(sendq) != 0) {
			stats_error_log("tcpclient[%s]: Unable to allocate additional memory for send queue, dropping data", client->name);
			return 4;
		}
	}
	memcpy(buffer_tail(sendq), buf, len);
	buffer_produced(sendq, len);

	if (client->state == STATE_CONNECTED) {
		client->write_watcher.started = true;
		ev_io_start(client->loop, &client->write_watcher.watcher);
	}
	return 0;
}

void tcpclient_destroy(tcpclient_t *client, int drop_queue) {
	if (client == NULL) {
		return;
	}
	ev_timer_stop(client->loop, &client->timeout_watcher);
	if (client->connect_watcher.started) {
		stats_debug_log("tcpclient_destroy: stopping connect watcher");
		ev_io_stop(client->loop, &client->connect_watcher.watcher);
		client->connect_watcher.started = false;
	}
	if (client->read_watcher.started) {
		stats_debug_log("tcpclient_destroy: stopping read watcher");
		ev_io_stop(client->loop, &client->read_watcher.watcher);
		client->read_watcher.started = false;
	}
	if (client->write_watcher.started) {
		stats_debug_log("tcpclient_destroy: stopping write watcher");
		ev_io_stop(client->loop, &client->write_watcher.watcher);
		client->write_watcher.started = false;
	}
		stats_debug_log("closing client->sd %d", client->sd);
	close(client->sd);
	if (client->addr != NULL) {
		freeaddrinfo(client->addr);
	}
	buffer_destroy(&client->send_queue);

	free(client->host);
	free(client->port);
	free(client->protocol);
}
