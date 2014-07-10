#include <sys/types.h>
#include <sys/socket.h>
#include <netdb.h>
#include <errno.h>
#include <fcntl.h>

#include <inttypes.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <stdio.h>
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
	GHashTable *ketama_cache;
	struct ev_loop *loop;

	uint64_t bytes_recv_udp;
	uint64_t bytes_recv_tcp;
	uint64_t total_connections;
	uint64_t malformed_lines;
	time_t last_reload;
};

typedef struct {
	stats_server_t *server;
	buffer_t buffer;
	int sd;
} stats_session_t;

typedef struct {
	tcpclient_t client;
	char *key;

	uint64_t bytes_queued;
	uint64_t bytes_sent;
	uint64_t relayed_lines;
	uint64_t dropped_lines;
} stats_backend_t;

static char *valid_stat_types[6] = {
	"c",
	"ms",
	"kv",
	"g",
	"h",
	"s"
};
static int valid_stat_types_len = 6;

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
	server->ketama_cache = g_hash_table_new_full(g_str_hash, g_str_equal, free, NULL);

	server->ketama_filename = filename;
	if(ketama_roll(&server->kc, filename) == 0) {
		stats_log(ketama_error());
		stats_log("stats: Unable to load ketama config from %s", filename);
		free(server);
		return NULL;
	}

	server->bytes_recv_udp = 0;
	server->bytes_recv_tcp = 0;
	server->malformed_lines = 0;
	server->total_connections = 0;
	server->last_reload = 0;

	return server;
}

void stats_kill_backend(stats_server_t *server, stats_backend_t *backend) {
	stats_log("stats: Killing backend %s", backend->key);
	free(backend->key);
	tcpclient_destroy(&backend->client, 1);
}

void stats_kill_all_backends(stats_server_t *server) {
	GHashTableIter iter;
	gpointer key, value;

	g_hash_table_iter_init(&iter, server->backends);
	while(g_hash_table_iter_next(&iter, &key, &value)) {
		stats_kill_backend(server, value);
		free(value);
		g_hash_table_iter_remove(&iter);
	}
}

void stats_server_reload(stats_server_t *server) {
	if(ketama_roll(&server->kc, server->ketama_filename) == 0) {
		stats_log(ketama_error());
		stats_log("stats: Unable to reload ketama config from %s", server->ketama_filename);
	}
	stats_kill_all_backends(server);
	g_hash_table_remove_all(server->ketama_cache);
	stats_log("stats: Reloaded from %s", server->ketama_filename);
	server->last_reload = time(NULL);
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
	session->server->total_connections++;
	session->sd = sd;

	return (void *)session;
}

int stats_sent(void *tcpclient, enum tcpclient_event event, void *context, char *data, size_t len) {
	stats_backend_t *backend = (stats_backend_t *)context;
	backend->bytes_sent += len;
	return 0;
}

stats_backend_t *stats_get_backend(stats_server_t *server, char *key, size_t keylen) {
	stats_backend_t *backend;
	char *address;
	char *port;
	int ret;

	size_t iplen;
	char *ip;
	char *pkey;
	mcs *ks;

	backend = g_hash_table_lookup(server->ketama_cache, key);
	if(backend != NULL) {
		return backend;
	}

	ks = ketama_get_server(key, server->kc);
	ip = ks->ip;
	iplen = sizeof(ks->ip);

	backend = g_hash_table_lookup(server->backends, ip);
	if(backend == NULL) {
		backend = malloc(sizeof(stats_backend_t));
		if(backend == NULL) {
			stats_log("stats: Cannot allocate memory for backend connection");
			return NULL;
		}

		backend->bytes_queued = 0;
		backend->bytes_sent = 0;
		backend->relayed_lines = 0;
		backend->dropped_lines = 0;

		if(tcpclient_init(&backend->client, server->loop, (void *)backend) != 0) {
			stats_log("stats: Unable to initialize tcpclient");
			free(backend);
			return NULL;
		};
		tcpclient_set_sent_callback(&backend->client, stats_sent);
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
	}

	pkey = malloc(keylen);
	memcpy(pkey, key, keylen);
	g_hash_table_insert(server->ketama_cache, pkey, backend);
	return backend;
}

int stats_validate_line(char *line, size_t len) {
	char *start, *end;
	char *err;
	size_t plen;
	char c;
	int i, valid;

	start = line;
	plen = len;
	end = memchr(start, ':', plen);
	if(end == NULL) {
		stats_log("stats: Invalid line \"%s\" missing ':'", line);
		return 1;
	}

	if((end - start) < 1) {
		stats_log("stats: Invalid line \"%s\" zero length key", line);
		return 2;
	}
	
	start = end + 1;
	plen = len - (start - line);

	c = end[0];
	end[0] = '\0';
	if((strtod(start, &err) == 0.0) && (err == start)) {
		end[0] = c;
		stats_log("stats: Invalid line \"%s\" unable to parse value as double", line);
		return 3;
	}
	end[0] = c;

	end = memchr(start, '|', plen);
	if(end == NULL) {
		stats_log("stats: Invalid line \"%s\" missing '|'", line);
		return 4;
	}

	start = end + 1;
	plen = len - (start - line);

	end = memchr(start, '@', plen);
	if(end != NULL) {
		c = end[0];
		end[0] = '\0';
		plen = end - start;
	}

	valid = 0;
	for(i = 0; i < valid_stat_types_len; i++) {
		if(strlen(valid_stat_types[i]) != plen) {
			continue;
		}
		if(strncmp(start, valid_stat_types[i], plen) == 0) {
			valid = 1;
			break;
		}
	}

	if(end != NULL) {
		end[0] = c;
		start = end + 1;
		plen = len - (start - line);
		if(plen == 0) {
			stats_log("stats: Invalid line \"%s\" @ sample with no rate", line);
			return 5;
		}
		if((strtod(start, &err) == 0.0) && err == start) {
			stats_log("stats: Invalid line \"%s\" invalid sample rate", line);
			return 6;
		}
	}

	if(valid == 0) {
		stats_log("stats: Invalid line \"%s\" unknown stat type \"%s\"", line, start);
		return 7;
	}

	return 0;
}

int stats_relay_line(char *line, size_t len, stats_server_t *ss) {
	stats_backend_t *backend;
	char *keyend;

	line[len] = '\0';

	if(stats_validate_line(line, len) != 0) {
		return 1;
	}

	keyend = memchr(line, ':', len);
	if(keyend == NULL) {
		ss->malformed_lines++;
		stats_log("stats: dropping malformed line: \"%s\"", line);
		return 1;
	}
	//keylen = keyend - line;
	
	keyend[0] = '\0';
	backend = stats_get_backend(ss, line, (keyend - line));
	keyend[0] = ':';
	line[len] = '\n';

	if(backend == NULL) {
		backend->dropped_lines++;
		return 1;
	}

	if(tcpclient_sendall(&backend->client, line, len+1) != 0) {
		backend->dropped_lines++;
		stats_log("stats: Error sending to backend %s", backend->key);
		return 2;
	}

	backend->bytes_queued += len+1;
	backend->relayed_lines++;

	return 0;
}

void stats_send_statistics(stats_session_t *session) {
	GHashTableIter iter;
	gpointer key, value;
	buffer_t *response;
	stats_backend_t *backend;
	ssize_t bytes_sent;

	response = create_buffer(65536);

	buffer_produced(response,
		snprintf((char *)buffer_tail(response), buffer_spacecount(response),
		"global bytes_recv_udp counter %" PRIu64 "\n",
		session->server->bytes_recv_udp));

	buffer_produced(response,
		snprintf((char *)buffer_tail(response), buffer_spacecount(response),
		"global bytes_recv_tcp counter %" PRIu64 "\n",
		session->server->bytes_recv_tcp));

	buffer_produced(response,
		snprintf((char *)buffer_tail(response), buffer_spacecount(response),
		"global total_connections counter %" PRIu64 "\n",
		session->server->total_connections));

	buffer_produced(response,
		snprintf((char *)buffer_tail(response), buffer_spacecount(response),
		"global last_reload timestamp %" PRIu64 "\n",
		session->server->last_reload));

	buffer_produced(response,
		snprintf((char *)buffer_tail(response), buffer_spacecount(response),
		"global malformed_lines counter %" PRIu64 "\n",
		session->server->malformed_lines));

	g_hash_table_iter_init(&iter, session->server->backends);
	while(g_hash_table_iter_next(&iter, &key, &value)) {
		backend = (stats_backend_t *)value;

		buffer_produced(response,
			snprintf((char *)buffer_tail(response), buffer_spacecount(response),
			"backend:%s bytes_queued counter %" PRIu64 "\n",
			backend->key, backend->bytes_queued));

		buffer_produced(response,
			snprintf((char *)buffer_tail(response), buffer_spacecount(response),
			"backend:%s bytes_sent counter %" PRIu64 "\n",
			backend->key, backend->bytes_sent));

		buffer_produced(response,
			snprintf((char *)buffer_tail(response), buffer_spacecount(response),
			"backend:%s relayed_lines counter %" PRIu64 "\n",
			backend->key, backend->relayed_lines));

		buffer_produced(response,
			snprintf((char *)buffer_tail(response), buffer_spacecount(response),
			"backend:%s dropped_lines counter %" PRIu64 "\n",
			backend->key, backend->dropped_lines));
	}

	buffer_produced(response,
		snprintf((char *)buffer_tail(response), buffer_spacecount(response), "\n"));

	while(buffer_datacount(response) > 0) {
		bytes_sent = send(session->sd, buffer_head(response), buffer_datacount(response), 0);
		if(bytes_sent < 0) {
			stats_log("stats: Error sending status response: %s", strerror(errno));
			break;
		}

		if(bytes_sent == 0) {
			stats_log("stats: Error sending status response: Client closed connection");
			break;
		}

		buffer_consume(response, bytes_sent);
	}
	delete_buffer(response);
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

		if(len >= 6 && memcmp(head, "status\n", 7) == 0) {
			stats_send_statistics(session);
		}else{
			stats_relay_line(head, len, session->server);
		}
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

	session->server->bytes_recv_tcp += bytes_read;

	if(buffer_produced(&session->buffer, bytes_read) != 0) {
		stats_log("stats: Unable to produce buffer by %i bytes, aborting", bytes_read);
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
	buffer_t *buffer;
	char *head, *tail;
	size_t len;

	buffer = create_buffer(MAX_UDP_LENGTH);

	bytes_read = read(sd, buffer_head(buffer), MAX_UDP_LENGTH);

	if(bytes_read == 0) {
		stats_log("stats: Got zero-length UDP payload. That's weird.");
		return 1;
	}

	if(bytes_read < 0) {
		if(errno == EAGAIN) {
			return 0;
		}
		stats_log("stats: Error calling recvfrom: %s", strerror(errno));
		return 2;
	}

	buffer_produced(buffer, bytes_read);
	ss->bytes_recv_udp += bytes_read;

	while(buffer_datacount(buffer) > 0) {
		head = (char *)buffer_head(buffer);
		tail = memchr(head, '\n', buffer_datacount(buffer));

		if(tail == NULL) {
			break;
		}

		len = tail - head;

		if(stats_relay_line(head, len, ss) != 0) {
			return 3;
		}
		buffer_consume(buffer, len + 1);	// Add 1 to include the '\n'
	}

	if(buffer_datacount(buffer) > 0) {
		stats_log("stats: Extra data still in buffer after processing UDP payload: \"%s\"", buffer_head(buffer));
	}

	delete_buffer(buffer);

	return 0;
}

void stats_server_destroy(stats_server_t *server) {
	stats_kill_all_backends(server);
	g_hash_table_destroy(server->ketama_cache);
	g_hash_table_destroy(server->backends);

	ketama_smoke(server->kc);
	free(server);
}


