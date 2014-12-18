#include "./server.h"

#include "./log.h"

#include <ev.h>
#include <string.h>

static void init_server(struct server *server) {
	server->enabled = false;
	server->server = NULL;
	server->ts = NULL;
	server->us = NULL;
}

static bool connect_server(struct server *server,
			   struct proto_config *config,
			   protocol_parser_t parser,
			   validate_line_validator_t validator) {
	server->enabled = true;

	struct ev_loop *loop = ev_default_loop(0);

	server->server = stats_server_create(
		loop, config, parser, validator);

	if (server->server == NULL) {
		stats_error_log("main: Unable to create stats_server");
		return false;
	}

	if (stats_num_backends(server->server) == 0) {
		stats_error_log("server has no backends, something seems wrong");
		return false;
	}

	server->ts = tcpserver_create(loop, server->server);
	if (server->ts == NULL) {
		stats_error_log("failed to create tcpserver");
		return false;
	}

	server->us = udpserver_create(loop, server->server);
	if (server->us == NULL) {
		stats_error_log("failed to create udpserver");
		return false;
	}

	if (tcpserver_bind(server->ts, config->bind, stats_connection, stats_recv) != 0) {
		stats_error_log("unable to bind tcp %s", config->bind);
		return false;
	}
	if (udpserver_bind(server->us, config->bind, stats_udp_recv) != 0) {
		stats_error_log("unable to bind udp %s", config->bind);
		return false;
	}
	return true;
}

static void destroy_server(struct server *server) {
	if (!server->enabled) {
		return;
	}
	if (server->ts != NULL) {
		tcpserver_destroy(server->ts);
	}
	if (server->us != NULL) {
		udpserver_destroy(server->us);
	}
	if (server->server != NULL) {
		stats_server_destroy(server->server);
	}
}

void init_server_collection(struct server_collection *server_collection,
			    const char *filename) {
	server_collection->initialized = true;
	server_collection->config_file = strdup(filename);
	init_server(&server_collection->carbon_server);
	init_server(&server_collection->statsd_server);
}

void connect_server_collection(struct server_collection *server_collection,
			       struct config *config) {
	connect_server(&server_collection->carbon_server,
		       &config->carbon_config,
		       protocol_parser_carbon,
		       validate_carbon);
	connect_server(&server_collection->statsd_server,
		       &config->statsd_config,
		       protocol_parser_statsd,
		       validate_statsd);
}

void destroy_server_collection(struct server_collection *server_collection) {
	if (server_collection->initialized) {
		free(server_collection->config_file);
		destroy_server(&server_collection->carbon_server);
		destroy_server(&server_collection->statsd_server);
		server_collection->initialized = false;
	}
}
