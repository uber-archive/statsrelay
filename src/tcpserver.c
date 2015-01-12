#include "tcpserver.h"
#include "log.h"

#include <stdio.h>

#include <arpa/inet.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netdb.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <unistd.h>
#include <fcntl.h>
#include <string.h>

#include <ev.h>

#define MAX_TCP_HANDLERS 32
#define LISTEN_BACKLOG 128

typedef struct tcplistener_t tcplistener_t;
typedef struct tcpsession_t tcpsession_t;

// tcpserver_t represents an event loop bound to multiple sockets
struct tcpserver_t {
	struct ev_loop *loop;
	tcplistener_t *listeners[MAX_TCP_HANDLERS];
	int listeners_len;
	void *data;
};

// tcplistener_t represents a socket listening on a port
struct tcplistener_t {
	struct ev_loop *loop;
	int sd;
	struct ev_io *watcher;
	void *data;
	void *(*cb_conn)(int, void *);
	int (*cb_recv)(int, void *, void *);
};

// tcpsession_t represents a client connection to the server
struct tcpsession_t {
	struct ev_loop *loop;
	int sd;
	struct ev_io *watcher;
	void *data;
	int (*cb_recv)(int, void *, void *);
	struct sockaddr_storage client_addr;
	void *ctx;
	void (*ctx_dealloc)(void *);
};

static tcpsession_t *tcpsession_create(tcplistener_t *listener) {
	tcpsession_t *session;

	session = malloc(sizeof(tcpsession_t));
	if (session == NULL) {
		return NULL;
	}

	session->watcher = malloc(sizeof(struct ev_io));
	if (session->watcher == NULL) {
		free(session);
		return NULL;
	}

	session->loop = listener->loop;
	session->data = listener->data;
	session->sd = -1;
	session->cb_recv = listener->cb_recv;
	session->watcher->data = (void *)session;
	session->ctx = NULL;
	session->ctx_dealloc = NULL;
	return session;
}

static void tcpsession_destroy(tcpsession_t *session) {
	if (session->sd > 0) {
		close(session->sd);
	}
	ev_io_stop(session->loop, session->watcher);
	free(session->watcher);
	free(session);
}

// Called every time the session socket is readable (data available)
// if you don't consume it, it'll get called again very quickly
static void tcpsession_recv_callback(struct ev_loop *loop,
				     struct ev_io *watcher,
				     int revents) {
	tcpsession_t *session;

	if (revents & EV_ERROR) {
		// ev(3) says this is an error of "unspecified" type, so
		// that's bloody useful.
		stats_error_log("tcpsession: libev server socket error");
		return;
	}

	session = (tcpsession_t *)watcher->data;
	if (session == NULL) {
		stats_error_log("tcpsession: Unable to determine tcpsession, not calling recv callback");
		return;
	}

	if (session->cb_recv == NULL) {
		stats_error_log("tcpsession: No recv callback registered for session, ignoring event");
		return;
	}

	if (session->cb_recv(session->sd, session->data, session->ctx) != 0) {
		//stats_error_log("tcpsession: recv callback returned non-zero, closing connection");
		tcpsession_destroy(session);
		return;
	}
}

// Called every time the server socket is readable (new connection to be accepted)
// if you don't consume it, it'll get called again very quickly
static void tcplistener_accept_callback(struct ev_loop *loop,
					struct ev_io *watcher,
					int revents) {
	stats_debug_log("in tcplistener_accept_callback");
	socklen_t sin_size;
	tcplistener_t *listener;
	tcpsession_t *session;
	int err;

	if (revents & EV_ERROR) {
		// ev(3) says this is an error of "unspecified" type, so
		// that's bloody useful.
		stats_error_log("tcplistener: libev server socket error");
		return;
	}

	listener = (tcplistener_t *) watcher->data;
	session = tcpsession_create(listener);
	if (session == NULL) {
		stats_error_log("tcplistener: Unable to allocate tcpsession, not calling accept()");
		return;
	}

	sin_size = sizeof(session->client_addr);
	session->sd = accept(watcher->fd, (struct sockaddr *)&session->client_addr, &sin_size);
	stats_debug_log("tcpserver: accepted new tcp client connection, client fd = %d, tcp server fd = %d", session->sd, watcher->fd);
	if (session->sd < 0) {
		stats_error_log("tcplistener: Error accepting connection: %s", strerror(errno));
		return;
	}

	err = fcntl(session->sd, F_SETFL, (fcntl(session->sd, F_GETFL) | O_NONBLOCK));
	if (err != 0) {
		stats_error_log("tcplistener: Error setting socket to non-blocking: %s", strerror(errno));
		return;
	}

	session->ctx = listener->cb_conn(session->sd, session->data);

	ev_io_init(session->watcher, tcpsession_recv_callback, session->sd, EV_READ);
	ev_io_start(loop, session->watcher);
}


tcpserver_t *tcpserver_create(struct ev_loop *loop, void *data) {
	tcpserver_t *server;
	server = (tcpserver_t *) malloc(sizeof(tcpserver_t));
	server->loop = ev_default_loop(0);
	server->listeners_len = 0;
	server->data = data;
	return server;
}


static tcplistener_t *tcplistener_create(tcpserver_t *server,
					 struct addrinfo *addr,
					 void *(*cb_conn)(int, void *),
					 int (*cb_recv)(int, void *, void *)) {
	tcplistener_t *listener;
	char addr_string[INET6_ADDRSTRLEN];
	void *ip;
	int port;
	int yes = 1;
	int err;

	listener = malloc(sizeof(tcplistener_t));
	listener->loop = server->loop;
	listener->data = server->data;
	listener->cb_conn = cb_conn;
	listener->cb_recv = cb_recv;
	listener->sd = socket(
				addr->ai_family,
				addr->ai_socktype,
				addr->ai_protocol);

	memset(addr_string, 0, INET6_ADDRSTRLEN);
	if (addr->ai_family == AF_INET) {
		struct sockaddr_in *ipv4 = (struct sockaddr_in *)addr->ai_addr;
		ip = &(ipv4->sin_addr);
		port = ntohs(ipv4->sin_port);
	} else {
		struct sockaddr_in6 *ipv6 = (struct sockaddr_in6 *)addr->ai_addr;
		ip = &(ipv6->sin6_addr);
		port = ntohs(ipv6->sin6_port);
	}
	if (inet_ntop(addr->ai_family, ip, addr_string, addr->ai_addrlen) == NULL) {
		stats_error_log("tcplistener: Unable to format network address string");
		free(listener);
		return NULL;
	}

	if (listener->sd < 0) {
		stats_error_log("tcplistener: Error creating socket %s[:%i]: %s", addr_string, port, strerror(errno));
		free(listener);
		return NULL;
	}

	err = setsockopt(listener->sd, SOL_SOCKET, SO_REUSEADDR, &yes, sizeof(int));
	if (err != 0) {
		stats_error_log("tcplistener: Error setting SO_REUSEADDR on %s[:%i]: %s", addr_string, port, strerror(errno));
		free(listener);
		return NULL;
	}

	err = fcntl(listener->sd, F_SETFL, (fcntl(listener->sd, F_GETFL) | O_NONBLOCK));
	if (err != 0) {
		stats_error_log("tcplistener: Error setting socket to non-blocking for %s[:%i]: %s", addr_string, port, strerror(errno));
		free(listener);
		return NULL;
	}

	err = bind(listener->sd, addr->ai_addr, addr->ai_addrlen);
	if (err != 0) {
		stats_error_log("tcplistener: Error binding socket for %s[:%i]: %s", addr_string, port, strerror(errno));
		free(listener);
		return NULL;
	}

	err = listen(listener->sd, LISTEN_BACKLOG);
	if (err != 0) {
		stats_error_log("tcplistener: Error listening to socket %s[:%i]: %s", addr_string, port, strerror(errno));
		free(listener);
		return NULL;
	}

	listener->watcher = malloc(sizeof(struct ev_io));
	listener->watcher->data = (void *) listener;

	ev_io_init(listener->watcher, tcplistener_accept_callback, listener->sd, EV_READ);
	stats_log("tcpserver: Listening on frontend %s[:%i], fd = %d",
		  addr_string, port, listener->sd);

	return listener;
}


static void tcplistener_destroy(tcpserver_t *server, tcplistener_t *listener) {
	if (listener->watcher != NULL) {
		ev_io_stop(server->loop, listener->watcher);
		free(listener->watcher);
	}
	free(listener);
}


int tcpserver_bind(tcpserver_t *server,
		   const char *address_and_port,
		   void *(*cb_conn)(int, void *),
		   int (*cb_recv)(int, void *, void *)) {
	tcplistener_t *listener;
	struct addrinfo hints;
	struct addrinfo *addrs, *p;
	int err;

	char *address = strdup(address_and_port);
	if (address == NULL) {
		stats_error_log("tcpserver: strdup(3) failed");
		return 1;
	}

	char *ptr = strrchr(address_and_port, ':');
	if (ptr == NULL) {
		free(address);
		stats_error_log("tcpserver: missing port");
		return 1;
	}
	const char *port = ptr + 1;
	address[ptr - address_and_port] = '\0';

	memset(&hints, 0, sizeof(struct addrinfo));
	hints.ai_family = AF_UNSPEC;
	hints.ai_socktype = SOCK_STREAM;
	hints.ai_flags = AI_PASSIVE;

	err = getaddrinfo(address, port, &hints, &addrs);
	if (err != 0) {
		free(address);
		stats_error_log("tcpserver: getaddrinfo error: %s", gai_strerror(err));
		return 1;
	}

	for (p = addrs; p != NULL; p = p->ai_next) {
		if (server->listeners_len >= MAX_TCP_HANDLERS) {
			stats_error_log("tcpserver: Unable to create more than %i TCP listeners", MAX_TCP_HANDLERS);
			free(address);
			freeaddrinfo(addrs);
			return 1;
		}
		listener = tcplistener_create(server, p, cb_conn, cb_recv);
		if (listener == NULL) {
			continue;
		}
		server->listeners[server->listeners_len] = listener;
		server->listeners_len++;
		ev_io_start(server->loop, listener->watcher);
	}

	free(address);
	freeaddrinfo(addrs);
	return 0;
}

void tcpserver_destroy(tcpserver_t *server) {
	for (int i = 0; i < server->listeners_len; i++) {
		tcplistener_destroy(server, server->listeners[i]);
	}
	free(server);
}
