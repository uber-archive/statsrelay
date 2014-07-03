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

#include "ketama.h"

#include "tcpclient.h"
#include "buffer.h"
#include "stats.h"
#include "log.h"


struct stats_server_t {
	char *ketama_filename;
	ketama_continuum kc;
	GHashTable *backends;
	struct ev_loop *loop;
};

typedef struct {
	stats_server_t *server;
	buffer_t buffer;
} stats_session_t;

typedef struct {
	tcpclient_t client;
	char *key;
} stats_backend_t;


stats_server_t *stats_server_create(char *filename, struct ev_loop *loop) {
	stats_server_t *server;

	server = malloc(sizeof(stats_server_t));
	if(server == NULL) {
		stats_log("stats: Unable to allocate memory");
		free(server);
		return NULL;
	}

	server->loop = loop;
	server->backends = g_hash_table_new(g_str_hash, g_str_equal);

	server->ketama_filename = filename;
	if(ketama_roll(&server->kc, filename) == 0) {
		stats_log(ketama_error());
		stats_log("stats: Unable to load ketama config from %s", filename);
		free(server);
		return NULL;
	}

	return server;
}

void stats_server_reload(stats_server_t *server) {
	if(ketama_roll(&server->kc, server->ketama_filename) == 0) {
		stats_log(ketama_error());
		stats_log("stats: Unable to reload ketama config from %s", server->ketama_filename);
	}
	stats_log("stats: Reloaded from %s", server->ketama_filename);
}

void *stats_connection(int sd, void *ctx) {
	stats_session_t *session;

	stats_log("stats: Accepted connection on socket %i", sd);
	session = (stats_session_t *)malloc(sizeof(stats_session_t));
	if(session == NULL) {
		stats_log("stats: Unable to allocate memory");
		return NULL;
	}

	if(buffer_init(&session->buffer) != 0) {
		stats_log("stats: Unable to initialize buffer");
		free(session);
		return NULL;
	}

	session->server = (stats_server_t *)ctx;

	return (void *)session;
}

stats_backend_t *stats_get_backend(stats_server_t *server, char *ip, size_t iplen) {
	stats_backend_t *backend;
	char *address;
	char *port;
	int ret;

	backend = g_hash_table_lookup(server->backends, ip);
	if(backend == NULL) {
		backend = malloc(sizeof(stats_backend_t));
		if(backend == NULL) {
			stats_log("stats: Cannot allocate memory for backend connection");
			return NULL;
		}
		if(tcpclient_init(&backend->client, server->loop) != 0) {
			stats_log("stats: Unable to initialize tcpclient");
			free(backend);
			return NULL;
		};
		g_hash_table_insert(server->backends, ip, backend);

		// Make a copy of the address because it's immutableish
		address = malloc(iplen);
		if(address == NULL) {
			stats_log("stats: Unable to allocate memory for backend address");
			return NULL;
		}
		memcpy(address, ip, iplen);
		backend->key = address;

		port = memchr(address, ':', iplen);
		if(port == NULL) {
			stats_log("stats: Unable to parse server address from config: %s", ip);
			free(address);
			return NULL;
		}
		port[0] = '\0';
		port++;

		ret = tcpclient_connect(&backend->client, address, port);
		if(ret != 0) {
			stats_log("stats: Error connecting to [%s]:%s (tcpclient_connect returned %i)", address, port, ret);
			free(address);
			return NULL;
		}

		port--;
		port[0] = ':';

		return backend;
	}

	return backend;
}

void stats_kill_backend(stats_server_t *server, stats_backend_t *backend) {
	stats_log("stats: Killing backend %s", backend->key);
	free(backend->key);
	tcpclient_destroy(&backend->client, 1);
}

int stats_relay_line(char *line, size_t len, stats_server_t *ss) {
	stats_backend_t *backend;

	char *keyend;
	//size_t keylen;
	mcs *server;

	line[len] = '\0';

	keyend = memchr(line, ':', len);
	if(keyend == NULL) {
		stats_log("stats: dropping malformed line: \"%s\"", line);
		return 1;
	}
	//keylen = keyend - line;

	keyend[0] = '\0';
	server = ketama_get_server(line, ss->kc);
	keyend[0] = ':';
	line[len] = '\n';

	backend = stats_get_backend(ss, server->ip, sizeof(server->ip));
	if(backend == NULL) {
		return 1;
	}

	if(tcpclient_sendall(&backend->client, line, len+1) != 0) {
		stats_log("stats: Error sending to backend %s", backend->key);
		return 2;
	}

	return 0;
}

int stats_process_lines(stats_session_t *session) {
	char *head, *tail;
	size_t len;

	while(buffer_datacount(&session->buffer) > 0) {
		head = (char *)buffer_head(&session->buffer);
		tail = memchr(head, '\n', buffer_datacount(&session->buffer));

		if(tail == NULL) {
			break;
		}

		len = tail - head;
		stats_relay_line(head, len, session->server);
		buffer_consume(&session->buffer, len + 1);	// Add 1 to include the '\n'
	}

	return 0;
}

void stats_session_destroy(stats_session_t *session) {
	buffer_destroy(&session->buffer);
	free(session);
}

int stats_recv(int sd, void *data, void *ctx) {
	stats_session_t *session = (stats_session_t *)ctx;

	ssize_t bytes_read;
	size_t space;

	space = buffer_spacecount(&session->buffer);
	if(space == 0) {
		if(buffer_expand(&session->buffer) != 0) {
			stats_log("stats: Unable to expand buffer, aborting");
			stats_session_destroy(session);
			return 1;
		}
		space = buffer_spacecount(&session->buffer);
	}

	bytes_read = recv(sd, buffer_tail(&session->buffer), space, 0);
	if(bytes_read < 0) {
		stats_log("stats: Error receiving from socket: %s", strerror(errno));
		stats_session_destroy(session);
		return 2;
	}

	if(bytes_read == 0) {
		stats_log("stats: Client closed connection");
		stats_session_destroy(session);
		return 3;
	}

	if(buffer_produced(&session->buffer, bytes_read) != 0) {
		stats_log("stats: Unable to consume buffer by %i bytes, aborting", bytes_read);
		stats_session_destroy(session);
		return 4;
	}

	if(stats_process_lines(session) != 0) {
		stats_log("stats: Invalid line processed, closing connection");
		stats_session_destroy(session);
		return 5;
	}

	return 0;
}

int stats_udp_recv(int sd, void *data) {
	stats_server_t *ss = (stats_server_t *)data;
	ssize_t bytes_read;
	char buffer[MAX_UDP_LENGTH];

	bytes_read = recvfrom(sd, buffer, MAX_UDP_LENGTH, 0, NULL, NULL);

	if(bytes_read == 0) {
		stats_log("stats: Got zero-length UDP payload. That's weird.");
		return 1;
	}

	if(bytes_read < 0) {
		stats_log("stats: Error calling recvfrom: %s", strerror(errno));
		return 2;
	}

	if(stats_relay_line(buffer, bytes_read, ss) != 0) {
		return 3;
	}

	return 0;
}

void stats_server_destroy(stats_server_t *server) {
	GHashTableIter iter;
	gpointer key, value;

	g_hash_table_iter_init(&iter, server->backends);
	while(g_hash_table_iter_next(&iter, &key, &value)) {
		stats_kill_backend(server, value);
		free(value);
		g_hash_table_iter_remove(&iter);
	}
	g_hash_table_destroy(server->backends);

	ketama_smoke(server->kc);
	free(server);
}


