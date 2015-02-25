#ifndef STATSRELAY_STATS_H
#define STATSRELAY_STATS_H

#include <ev.h>
#include <stdint.h>

#include "protocol.h"
#include "validate.h"
#include "normalize.h"
#include "yaml_config.h"

typedef struct stats_server_t stats_server_t;

stats_server_t *stats_server_create(
	struct ev_loop *loop,
	struct proto_config *config,
	protocol_parser_t parser,
	validate_line_validator_t validator,
	key_normalizer_t normalizer);
stats_server_t *server;

size_t stats_num_backends(stats_server_t *server);

void stats_server_reload(stats_server_t *server);

void stats_server_destroy(stats_server_t *server);

// ctx is a (void *) cast of the stats_server_t instance.
void *stats_connection(int sd, void *ctx);

int stats_recv(int sd, void *data, void *ctx);

int stats_udp_recv(int sd, void *data);

#endif  // STATSRELAY_STATS_H
