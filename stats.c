#include <sys/types.h>
#include <sys/socket.h>
#include <netdb.h>
#include <errno.h>

#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <time.h>

#include <glib.h>

#include "ketama.h"

#include "buffer.h"
#include "stats.h"
#include "log.h"

struct stats_server_t {
	char *ketama_filename;
	ketama_continuum kc;
	GHashTable *backends;
	GHashTable *backoff_timers;
};

typedef struct {
	stats_server_t *server;
	buffer_t buffer;
} stats_session_t;

typedef struct {
	struct addrinfo *addr;
	char *key;
	int sd;
} stats_backend_t;

typedef struct {
	time_t last_kill;
	int retry_count;
} stats_backoff_timer_t;


stats_server_t *stats_server_create(char *filename) {
	stats_server_t *server;

	server = malloc(sizeof(stats_server_t));
	if(server == NULL) {
		stats_log("stats: Unable to allocate memory");
		free(server);
		return NULL;
	}

	server->backends = g_hash_table_new(g_str_hash, g_str_equal);
	server->backoff_timers = g_hash_table_new(g_str_hash, g_str_equal);

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
	stats_log("Accepted connection on socket %i", sd);
	stats_session_t *session;

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
	stats_backoff_timer_t *backoff;
	struct addrinfo hints;
	struct addrinfo *addr;
	struct linger;
	char *address;
	char *port;
	int sd;

	backend = g_hash_table_lookup(server->backends, ip);
	if(backend == NULL) {
		backoff = g_hash_table_lookup(server->backoff_timers, ip);
		if(backoff == NULL) {
			backoff = malloc(sizeof(stats_backoff_timer_t));
			if(backoff == NULL) {
				stats_log("stats: Cannot allocate memory for backend connection");
				return NULL;
			}

			backoff->last_kill = 0;
			backoff->retry_count = 0;
			g_hash_table_insert(server->backoff_timers, ip, backoff);
		}else{
			if((time(NULL) - backoff->last_kill) < BACKEND_RETRY_TIMEOUT) {
				return NULL;
			}
			backoff->retry_count++;
			stats_log("stats: Retrying connection to backend %s #%i", ip, backoff->retry_count);
		}

		backend = malloc(sizeof(stats_backend_t));
		if(backend == NULL) {
			stats_log("stats: Cannot allocate memory for backend connection");
			return NULL;
		}

		// Make a copy of the address because it's immutableish
		address = malloc(iplen);
		if(address == NULL) {
			stats_log("stats: Cannot allocate memory for backend address");
			free(backend);
			free(backoff);
			return NULL;
		}
		memcpy(address, ip, iplen);

		port = memchr(address, ':', iplen);
		if(port == NULL) {
			stats_log("stats: Unable to parse server address from config: %s", ip);
			free(backend);
			free(backoff);
			free(address);
			return NULL;
		}
		port[0] = '\0';
		port++;

		memset(&hints, 0, sizeof hints);	// make sure the struct is empty
		hints.ai_family = AF_INET;			// ipv4
		hints.ai_socktype = SOCK_DGRAM;		// udp
		hints.ai_flags = AI_PASSIVE;		// fill in my IP for me
		if(getaddrinfo(address, port, &hints, &addr) != 0) {
			stats_log("stats: Error resolving backend %s: %s", address, gai_strerror(errno));
			free(backend);
			free(backoff);
			free(address);
			return NULL;
		}
		
		port--;
		port[0] = ':';

		sd = socket(addr->ai_family, addr->ai_socktype, addr->ai_protocol);
		if(sd == -1) {
			stats_log("stats: Unable to open backend socket: %s", strerror(errno));
			freeaddrinfo(addr);
			free(backend);
			free(backoff);
			free(address);
			return NULL;
		}

		if(connect(sd, addr->ai_addr, addr->ai_addrlen) != 0) {
			stats_log("stats: Unable to connect to %s: %s", address, strerror(errno));
			close(sd);
			freeaddrinfo(addr);
			free(backend);
			free(backoff);
			free(address);
			return NULL;
		}

		backend->key = address;
		backend->addr = addr;
		backend->sd = sd;
		g_hash_table_insert(server->backends, backend->key, backend);
		stats_log("stats: Connected to backend %s", backend->key);
	}
	return backend;
}

void stats_kill_backend(stats_server_t *server, stats_backend_t *backend, int remove_from_hash) {
	stats_backoff_timer_t *backoff;

	stats_log("stats: Killing backend %s", backend->key);
	if(remove_from_hash) {
		if(!g_hash_table_remove(server->backends, backend->key)) {
			stats_log("stats: Backend %s could not be removed from backends list! This will probably leak memory, cause a double-free, kill your loved ones and crash.", backend->key);
		}
	}

	backoff = g_hash_table_lookup(server->backoff_timers, backend->key);
	if(backoff != NULL) {
		backoff->last_kill = time(NULL);
	}
	
	shutdown(backend->sd, SHUT_RDWR);
	free(backend->key);
	freeaddrinfo(backend->addr);
	free(backend);
}

int stats_relay_line(char *line, size_t len, stats_session_t *session) {
	stats_backend_t *backend;
	ssize_t sent;

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
	server = ketama_get_server(line, session->server->kc);
	keyend[0] = ':';
	line[len] = '\n';

	backend = stats_get_backend(session->server, server->ip, sizeof(server->ip));
	if(backend == NULL) {
		return 1;
	}

	sent = send(backend->sd, line, len, 0);
	if(sent == -1) {
		stats_log("stats: Error sending line: %s", strerror(errno));
		stats_kill_backend(session->server, backend, 1);
		return 1;
	}
	if(sent != len) {
		stats_log("stats: Sent %i/%i bytes", sent, len);
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
		stats_relay_line(head, len, session);
		buffer_consume(&session->buffer, len + 1);	// Add 1 to include the '\n'
	}

	return 0;
}

void stats_session_destroy(stats_session_t *session) {
	stats_log("stats: Closing connection");
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

void stats_server_destroy(stats_server_t *server) {
	GHashTableIter iter;
	gpointer key, value;

	g_hash_table_iter_init(&iter, server->backends);
	while(g_hash_table_iter_next(&iter, &key, &value)) {
		stats_kill_backend(server, value, 0);
		g_hash_table_iter_remove(&iter);
	}
	g_hash_table_destroy(server->backends);

	ketama_smoke(server->kc);
	free(server);
}


