#ifndef STATS_H
#define STATS_H

#include <glib.h>
#include <ev.h>
#include <stdint.h>
#include "ketama.h"

#define BACKEND_RETRY_TIMEOUT 5
#define MAX_UDP_LENGTH 65536

typedef struct stats_server_t stats_server_t;

stats_server_t *stats_server_create(char *filename, struct ev_loop *loop);
void stats_set_max_send_queue(stats_server_t *server, uint64_t size);
void stats_set_validate_lines(stats_server_t *server, int validate_lines);
void stats_server_reload(stats_server_t *server);
void stats_server_destroy(stats_server_t *server);

// ctx is a (void *) cast of the stats_server_t instance.
void *stats_connection(int sd, void *ctx);
int stats_recv(int sd, void *data, void *ctx);
int stats_udp_recv(int sd, void *data);

#endif
