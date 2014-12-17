#include <sys/types.h>
#include <sys/socket.h>
#include <netdb.h>
#include <errno.h>
#include <fcntl.h>

#include <assert.h>
#include <inttypes.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <stdio.h>
#include <time.h>

#include "./hashring.h"
#include "./buffer.h"
#include "./log.h"
#include "./stats.h"
#include "./tcpclient.h"
#include "./validate.h"

#define BACKEND_RETRY_TIMEOUT 5
#define MAX_UDP_LENGTH 65536

typedef struct {
	tcpclient_t client;
	char *key;
	uint64_t bytes_queued;
	uint64_t bytes_sent;
	uint64_t relayed_lines;
	uint64_t dropped_lines;
	int failing;
} stats_backend_t;

struct stats_server_t {
	struct ev_loop *loop;

	uint64_t max_send_queue;
	int validate_lines;

	uint64_t bytes_recv_udp;
	uint64_t bytes_recv_tcp;
	uint64_t total_connections;
	uint64_t malformed_lines;
	time_t last_reload;

	size_t num_backends;
	stats_backend_t **backend_list;

	hashring_t ring;
	protocol_parser_t parser;
	validate_line_validator_t validator;
};

typedef struct {
	stats_server_t *server;
	buffer_t buffer;
	int sd;
} stats_session_t;

// callback after bytes are sent
static int stats_sent(void *tcpclient,
		      enum tcpclient_event event,
		      void *context,
		      char *data,
		      size_t len) {
	stats_backend_t *backend = (stats_backend_t *) context;
	backend->bytes_sent += len;
	return 0;
}

// Add a backend to the backend list.
static int add_backend(stats_server_t *server, stats_backend_t *backend) {
	stats_backend_t **new_backends = realloc(
		server->backend_list, sizeof(stats_backend_t *) * (server->num_backends + 1));
	if (new_backends == NULL) {
		stats_log("add_backend: failed to realloc backends list");
		return 1;
	}
	server->backend_list = new_backends;
	server->backend_list[server->num_backends++] = backend;
	return 0;
}

// Find a backend in the backend list; this is used so we don't create
// duplicate backends. This is linear with the number of actual
// backends in the file which should be fine for any reasonable
// configuration (say, less than 10,000 backend statsite or carbon
// servers). Also note that while this is linear, it only happens
// during statsrelay initialization, not when running.
static stats_backend_t *find_backend(stats_server_t *server, const char *key) {
	for (size_t i = 0; i < server->num_backends; i++) {
		stats_backend_t *backend = server->backend_list[i];
		if (strcmp(backend->key, key) == 0) {
			return backend;
		}
	}
	return NULL;
}

// Make a backend, returning it from the backend list if it's already
// been created.
static void* make_backend(const char *host_and_port, void *data) {
	stats_backend_t *backend = NULL;
	char *full_key = NULL;

	// First we normalize so that the key is in the format
	// host:port:protocol
	char *host = NULL;
	char *port = NULL;
	char *protocol = NULL;

	const char *colon1 = strchr(host_and_port, ':');
	if (colon1 == NULL) {
		stats_log("failed to parse host/port in \"%s\"", host_and_port);
		goto make_err;
	}
	host = strndup(host_and_port, colon1 - host_and_port);
	if (host == NULL) {
		stats_log("stats: alloc error in host");
		goto make_err;
	}
	const char *colon2 = strchr(colon1 + 1, ':');
	if (colon2 == NULL) {
		port = strdup(colon1 + 1);
		protocol = strdup("tcp");  // use TCP by default
	} else {
		port = strndup(colon1 + 1, colon2 - colon1 - 1);
		protocol = strdup(colon2 + 1);
	}
	if (port == NULL || protocol == NULL) {
		stats_log("stats: alloc error in port/protocol");
		goto make_err;
	}

	if (colon2 == NULL) {
		const size_t hp_len = strlen(host_and_port);
		const size_t space_needed = hp_len + strlen(protocol) + 2;
		full_key = malloc(space_needed);
		if (full_key != NULL && snprintf(full_key, space_needed, "%s:%s", host_and_port, protocol) < 0) {
			stats_error_log("failed to snprintf");
			goto make_err;
		}
	} else {
		full_key = strdup(host_and_port);
	}
	if (full_key == NULL) {
		stats_error_log("failed to create backend key");
	}

	// Find the key in our list of backends
	stats_server_t *server = (stats_server_t *) data;
	backend = find_backend(server, full_key);
	if (backend != NULL) {
		free(host);
		free(port);
		free(protocol);
		free(full_key);
		return backend;
	}
	backend = malloc(sizeof(stats_backend_t));
	if (backend == NULL) {
		stats_log("stats: alloc error creating backend");
		goto make_err;
	}

	if (tcpclient_init(&backend->client,
			   server->loop,
			   backend,
			   server->max_send_queue)) {
		stats_log("stats: failed to tcpclient_init");
		goto make_err;
	}

	if (tcpclient_connect(&backend->client, host, port, protocol)) {
		stats_log("stats: failed to conect tcpclient");
		goto make_err;
	}
	backend->bytes_queued = 0;
	backend->bytes_sent = 0;
	backend->relayed_lines = 0;
	backend->dropped_lines = 0;
	backend->failing = 0;
	backend->key = full_key;
	tcpclient_set_sent_callback(&backend->client, stats_sent);
	add_backend(server, backend);
	stats_debug_log("initialized new backend %s", backend->key);

	free(host);
	free(port);
	free(protocol);
	return backend;

make_err:
	free(host);
	free(port);
	free(protocol);
	free(full_key);
	return NULL;
}


static void kill_backend(void *data) {
	stats_backend_t *backend = (stats_backend_t *) data;
	if (backend->key != NULL) {
		stats_debug_log("killing backend %s", backend->key);
		free(backend->key);
	}
	tcpclient_destroy(&backend->client, 1);
	free(backend);
}

stats_server_t *stats_server_create(struct ev_loop *loop,
				    struct proto_config *config,
				    protocol_parser_t parser,
				    validate_line_validator_t validator) {
	stats_server_t *server;
	server = malloc(sizeof(stats_server_t));
	if (server == NULL) {
		stats_log("stats: Unable to allocate memory");
		return NULL;
	}

	server->loop = loop;
	server->num_backends = 0;
	server->backend_list = NULL;
	server->max_send_queue = config->max_send_queue;
	server->ring = hashring_load_from_config(
		config, server, make_backend, kill_backend);
	if (server->ring == NULL) {
		stats_error_log("hashring_load_from_config failed");
		goto server_create_err;
	}

	server->bytes_recv_udp = 0;
	server->bytes_recv_tcp = 0;
	server->malformed_lines = 0;
	server->total_connections = 0;
	server->last_reload = 0;

	server->validate_lines = config->enable_validation;
	server->parser = parser;
	server->validator = validator;

	stats_debug_log("initialized server with %d backends, hashring size = %d",
			server->num_backends, hashring_size(server->ring));

	return server;

server_create_err:
	if (server != NULL) {
		hashring_dealloc(server->ring);
		free(server);
	}
	return NULL;
}

size_t stats_num_backends(stats_server_t *server) {
	return server->num_backends;
}

void stats_server_reload(stats_server_t *server) {
	hashring_dealloc(server->ring);

	free(server->backend_list);
	server->num_backends = 0;
	server->backend_list = NULL;

	server->last_reload = time(NULL);

	// FIXME
}

void *stats_connection(int sd, void *ctx) {
	stats_session_t *session;

	stats_debug_log("stats: accepted client connection on fd %d", sd);
	session = (stats_session_t *) malloc(sizeof(stats_session_t));
	if (session == NULL) {
		stats_log("stats: Unable to allocate memory");
		return NULL;
	}

	if (buffer_init(&session->buffer) != 0) {
		stats_log("stats: Unable to initialize buffer");
		free(session);
		return NULL;
	}

	session->server = (stats_server_t *) ctx;
	session->server->total_connections++;
	session->sd = sd;
	return (void *) session;
}

static int stats_relay_line(const char *line, size_t len, stats_server_t *ss) {
	if (ss->validate_lines && ss->validator != NULL) {
		if (ss->validator(line, len) != 0) {
			return 1;
		}
	}

	static char key_buffer[8192];
	size_t key_len = ss->parser(line, len);
	if (key_len == 0) {
		ss->malformed_lines++;
		stats_log("stats: failed to find key: \"%s\"", line);
		return 1;
	}
	memcpy(key_buffer, line, key_len);
	key_buffer[key_len] = '\0';

	stats_backend_t *backend = hashring_choose(ss->ring, key_buffer, NULL);

	if (backend == NULL) {
		return 1;
	}

	if (tcpclient_sendall(&backend->client, line, len + 1) != 0) {
		backend->dropped_lines++;
		if (backend->failing == 0) {
			stats_log("stats: Error sending to backend %s", backend->key);
			backend->failing = 1;
		}
		return 2;
	} else {
		backend->failing = 0;
	}

	backend->bytes_queued += len + 1;
	backend->relayed_lines++;

	return 0;
}

void stats_send_statistics(stats_session_t *session) {
	stats_backend_t *backend;
	ssize_t bytes_sent;

	// TODO: this only needs to be allocated once, not every time we send
	// statistics
	buffer_t *response = create_buffer(MAX_UDP_LENGTH);
	if (response == NULL) {
		stats_log("failed to allocate send_statistics buffer");
		return;
	}

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

	for (size_t i = 0; i < session->server->num_backends; i++) {
		backend = session->server->backend_list[i];

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

		buffer_produced(response,
			snprintf((char *)buffer_tail(response), buffer_spacecount(response),
			"backend:%s failing boolean %i\n",
			backend->key, backend->failing));
	}

	buffer_produced(response,
		snprintf((char *)buffer_tail(response), buffer_spacecount(response), "\n"));

	while (buffer_datacount(response) > 0) {
		bytes_sent = send(session->sd, buffer_head(response), buffer_datacount(response), 0);
		if (bytes_sent < 0) {
			stats_log("stats: Error sending status response: %s", strerror(errno));
			break;
		}

		if (bytes_sent == 0) {
			stats_log("stats: Error sending status response: Client closed connection");
			break;
		}

		buffer_consume(response, bytes_sent);
	}
	delete_buffer(response);
}

static int stats_process_lines(stats_session_t *session) {
	char *head, *tail;
	size_t len;

	static char line_buffer[MAX_UDP_LENGTH + 2];

	while (1) {
		size_t datasize = buffer_datacount(&session->buffer);
		if (datasize == 0) {
			break;
		}
		head = (char *)buffer_head(&session->buffer);
		tail = memchr(head, '\n', datasize);
		if (tail == NULL) {
			break;
		}
		len = tail - head;
		memcpy(line_buffer, head, len);
		memcpy(line_buffer + len, "\n\0", 2);

		if (strcmp(line_buffer, "status\n") == 0) {
			stats_send_statistics(session);
		} else if (stats_relay_line(line_buffer, len, session->server) != 0) {
			return 1;
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

	// First we try to realign the buffer (memmove so that head
	// and ptr match) If that fails, we double the size of the
	// buffer
	space = buffer_spacecount(&session->buffer);
	if (space == 0) {
		buffer_realign(&session->buffer);
		space = buffer_spacecount(&session->buffer);
		if (space == 0) {
			if (buffer_expand(&session->buffer) != 0) {
				stats_log("stats: Unable to expand buffer, aborting");
				goto stats_recv_err;
			}
			space = buffer_spacecount(&session->buffer);
		}
	}

	bytes_read = recv(sd, buffer_tail(&session->buffer), space, 0);
	if (bytes_read < 0) {
		stats_log("stats: Error receiving from socket: %s", strerror(errno));
		goto stats_recv_err;
	} else if (bytes_read == 0) {
		stats_debug_log("stats: client from fd %d closed conncetion", sd);
		goto stats_recv_err;
	} else {
		stats_debug_log("stats: received %zd bytes from tcp client fd %d", bytes_read, sd);
	}

	session->server->bytes_recv_tcp += bytes_read;

	if (buffer_produced(&session->buffer, bytes_read) != 0) {
		stats_log("stats: Unable to produce buffer by %i bytes, aborting", bytes_read);
		goto stats_recv_err;
	}

	if (stats_process_lines(session) != 0) {
		stats_log("stats: Invalid line processed, closing connection");
		goto stats_recv_err;
	}

	return 0;

stats_recv_err:
	stats_session_destroy(session);
	return 1;
}

// TODO: refactor this whole method to share more code with the tcp receiver:
//  * this shouldn't have to allocate a new buffer -- it should be on the ss
//  * the line processing stuff should use stats_process_lines()
int stats_udp_recv(int sd, void *data) {
	stats_server_t *ss = (stats_server_t *)data;
	ssize_t bytes_read;
	buffer_t *buffer;
	char *head, *tail;
	size_t len;

	static char line_buffer[MAX_UDP_LENGTH + 2];

	buffer = create_buffer(MAX_UDP_LENGTH);

	bytes_read = read(sd, buffer_head(buffer), MAX_UDP_LENGTH);

	if (bytes_read == 0) {
		stats_log("stats: Got zero-length UDP payload. That's weird.");
		goto udp_recv_err;
	}

	if (bytes_read < 0) {
		if (errno == EAGAIN) {
			stats_log("stats: interrupted during recvfrom");
			goto udp_recv_err;
		} else {
			stats_log("stats: Error calling recvfrom: %s", strerror(errno));
			goto udp_recv_err;
		}
	} else {
		stats_debug_log("stats: received %zd bytes from udp fd %d", bytes_read, sd);
	}

	if (buffer_produced(buffer, bytes_read) != 0) {
		stats_log("stats: failed to buffer_produced()\n");
		goto udp_recv_err;
	}
	ss->bytes_recv_udp += bytes_read;

	while (1) {
		int datasize = buffer_datacount(buffer);
		if (datasize <= 0) {
			break;
		}
		head = (char *) buffer_head(buffer);
		tail = memchr(head, '\n', datasize);
		if (tail == NULL) {
			break;
		}
		len = tail - head;
		memcpy(line_buffer, head, len);
		memcpy(line_buffer + len, "\n\0", 2);

		if (stats_relay_line(line_buffer, len, ss) != 0) {
			goto udp_recv_err;
		}
		buffer_consume(buffer, len + 1);  // Add 1 to include the '\n'
	}

	len = buffer_datacount(buffer);
	if (len > 0) {
		if (stats_relay_line(buffer_head(buffer), len, ss) != 0) {
			goto udp_recv_err;
		}
	}

	delete_buffer(buffer);
	return 0;

udp_recv_err:
	delete_buffer(buffer);
	return 1;
}

void stats_server_destroy(stats_server_t *server) {
	hashring_dealloc(server->ring);
	free(server->backend_list);
	server->num_backends = 0;
	free(server);
}
