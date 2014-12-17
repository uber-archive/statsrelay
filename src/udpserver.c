#include "udpserver.h"
#include "log.h"

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

#define MAX_UDP_HANDLERS 32

typedef struct udplistener_t udplistener_t;


// udpserver_t represents an event loop bound to multiple sockets
struct udpserver_t {
	struct ev_loop *loop;
	udplistener_t *listeners[MAX_UDP_HANDLERS];
	int listeners_len;
	void *data;
};

// udplistener_t represents a socket listening on a port
struct udplistener_t {
	struct ev_loop *loop;
	int sd;
	struct ev_io *watcher;
	void *data;
	int (*cb_recv)(int, void *);
};


udpserver_t *udpserver_create(struct ev_loop *loop, void *data) {
	udpserver_t *server;
	server = malloc(sizeof(udpserver_t));
	server->loop = loop;
	server->listeners_len = 0;
	server->data = data;
	return server;
}

static void udplistener_recv_callback(struct ev_loop *loop, struct ev_io *watcher, int revents) {
	udplistener_t *listener;
	listener = (udplistener_t *)watcher->data;

	if (revents & EV_ERROR) {
		stats_log("udplistener: libev server socket error");
		return;
	}

	if (listener->cb_recv(listener->sd, listener->data) != 0) {
		//stats_log("udplistener: recv callback returned non-zero");
		return;
	}
}

static udplistener_t *udplistener_create(udpserver_t *server, struct addrinfo *addr, int (*cb_recv)(int, void *)) {
	udplistener_t *listener;
	char addr_string[INET6_ADDRSTRLEN];
	void *ip;
	int port;
	int yes = 1;
	int err;

	listener = (udplistener_t *)malloc(sizeof(udplistener_t));
	listener->loop = server->loop;
	listener->data = server->data;
	listener->cb_recv = cb_recv;
	listener->sd = socket(addr->ai_family,
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
		stats_log("udplistener: Unable to format network address string");
		free(listener);
		return NULL;
	}

	if (listener->sd < 0) {
		stats_log("udplistener: Error creating socket %s[:%i]: %s", addr_string, port, strerror(errno));
		free(listener);
		return NULL;
	}

	err = setsockopt(listener->sd, SOL_SOCKET, SO_REUSEADDR, &yes, sizeof(int));
	if (err != 0) {
		stats_log("udplistener: Error setting SO_REUSEADDR on %s[:%i]: %s", addr_string, port, strerror(errno));
		free(listener);
		return NULL;
	}

	err = fcntl(listener->sd, F_SETFL, (fcntl(listener->sd, F_GETFL) | O_NONBLOCK));
	if (err != 0) {
		stats_log("udplistener: Error setting socket to non-blocking for %s[:%i]: %s", addr_string, port, strerror(errno));
		free(listener);
		return NULL;
	}

	err = bind(listener->sd, addr->ai_addr, addr->ai_addrlen);
	if (err != 0) {
		stats_log("udplistener: Error binding socket for %s[:%i]: %s", addr_string, port, strerror(errno));
		free(listener);
		return NULL;
	}

	listener->watcher = (struct ev_io *)malloc(sizeof(struct ev_io));
	listener->watcher->data = (void *)listener;

	ev_io_init(listener->watcher, udplistener_recv_callback, listener->sd, EV_READ);
	stats_log("udpserver: Listening on frontend %s[:%i], fd = %d", addr_string, port, listener->sd);

	return listener;
}


static void udplistener_destroy(udpserver_t *server, udplistener_t *listener) {
	if (listener->watcher != NULL) {
		ev_io_stop(server->loop, listener->watcher);
		free(listener->watcher);
	}
	free(listener);
}


int udpserver_bind(udpserver_t *server,
		   const char *address_and_port,
		   int (*cb_recv)(int, void *)) {
	udplistener_t *listener;
	struct addrinfo hints;
	struct addrinfo *addrs, *p;
	int err;

	char *address = strdup(address_and_port);
	if (address == NULL) {
		stats_log("udpserver: strdup(3) failed");
		return 1;
	}

	char *ptr = strrchr(address_and_port, ':');
	if (ptr == NULL) {
		stats_error_log("udpserver: missing port");
		return 1;
	}
	const char *port = ptr + 1;
	address[ptr - address_and_port] = '\0';

	memset(&hints, 0, sizeof(struct addrinfo));
	hints.ai_family = AF_UNSPEC;
	hints.ai_socktype = SOCK_DGRAM;
	hints.ai_flags = AI_PASSIVE;

	err = getaddrinfo(address, port, &hints, &addrs);
	if (err != 0) {
		free(address);
		stats_log("udpserver: getaddrinfo error: %s", gai_strerror(err));
		return 1;
	}

	for (p = addrs; p != NULL; p = p->ai_next) {
		if (server->listeners_len >= MAX_UDP_HANDLERS) {
			stats_log("udpserver: Unable to create more than %i UDP listeners", MAX_UDP_HANDLERS);
			free(address);
			freeaddrinfo(addrs);
			return 1;
		}
		if ((address == NULL) && (p->ai_family != AF_INET6)) {
			continue;
		}
		listener = udplistener_create(server, p, cb_recv);
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


void udpserver_destroy(udpserver_t *server) {
	int i;

	for (i = 0; i < server->listeners_len; i++) {
		udplistener_destroy(server, server->listeners[i]);
	}
	//ev_break(server->loop, EVBREAK_ALL);
	//ev_loop_destroy(server->loop);
	free(server);
}
