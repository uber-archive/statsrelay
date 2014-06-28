#include <sys/types.h>
#include <sys/socket.h>

#include <stdlib.h>
#include <string.h>
#include <errno.h>

#include "ketama.h"

#include "buffer.h"
#include "stats.h"
#include "list.h"
#include "log.h"

typedef struct {
	ketama_continuum *kc;
	buffer_t buffer;
} stats_session_t;


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

	session->kc = (ketama_continuum *)ctx;

	return (void *)session;
}

void stats_session_destroy(stats_session_t *session) {
	stats_log("stats: Closing connection");
	buffer_destroy(&session->buffer);
	free(session);
}

int stats_relay_line(char *line, size_t len, stats_session_t *session) {
	char *keyend;
	size_t keylen;
	mcs *server;

	line[len] = '\0';

	keyend = memchr(line, ':', len);
	if(keyend == NULL) {
		return 1;
	}
	keylen = keyend - line;

	keyend[0] = '\0';
	server = ketama_get_server(line, *session->kc);
	keyend[0] = ':';

	stats_log("stats: Would send line \"%s\" to server \"%s\"", line, server->ip);

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

