#include "config.h"
#include "protocol.h"
#include "tcpserver.h"
#include "udpserver.h"
#include "stats.h"
#include "log.h"
#include "validate.h"

#include <assert.h>
#include <inttypes.h>
#include <signal.h>
#include <string.h>
#include <unistd.h>
#include <stdlib.h>
#include <stdio.h>
#include <getopt.h>
#include <ev.h>

enum statsrelay_proto {
	STATSRELAY_PROTO_UNKNOWN = 0,
	STATSRELAY_PROTO_STATSD = 1,
	STATSRELAY_PROTO_CARBON = 2
};


static struct option long_options[] = {
	{"config",		required_argument,	NULL, 'c'},
	{"bind",		required_argument,	NULL, 'b'},
	{"verbose",		no_argument,		NULL, 'v'},
	{"protocol",		required_argument,	NULL, 'p'},
	{"help",		no_argument,		NULL, 'h'},
	{"max-send-queue",	required_argument,	NULL, 'q'},
	{"no-validation",	no_argument,		NULL, 'n'},
};

typedef struct statsrelay_options_t {
	const char *filename;
	int verbose;
	uint64_t max_send_queue;
	int validate_lines;
} statsrelay_options_t;

static stats_server_t *server = NULL;
static tcpserver_t *ts = NULL;
static udpserver_t *us = NULL;
static struct ev_loop *loop = NULL;

static const char default_protocol[] = "statsd";

static const char default_statsd_port[] = "8125";
static const char default_statsd_config[] = "/etc/statsrelay.conf";

static const char default_carbon_port[] = "2003";
static const char default_carbon_config[] = "/etc/carbonrelay.conf";

static const uint64_t default_max_send_queue = 134217728;

static void graceful_shutdown(struct ev_loop *loop, ev_signal *w, int revents) {
	stats_log("Received signal, shutting down.");
	ev_break(loop, EVBREAK_ALL);

	if (ts != NULL) {
		tcpserver_destroy(ts);
	}
	if (us != NULL) {
		udpserver_destroy(us);
	}
	if (server != NULL) {
		stats_server_destroy(server);
	}
}

static void reload_config(struct ev_loop *loop, ev_signal *w, int revents) {
	stats_log("Received SIGHUP, reloading.");
	if (server != NULL) {
		stats_server_reload(server);
	}
}


static int choose_protocol(enum statsrelay_proto *protocol, const char *proto_string) {
	if (strcmp(proto_string, "statsd") == 0) {
		*protocol = STATSRELAY_PROTO_STATSD;
		return 0;
	} else if (strcmp(proto_string, "carbon") == 0) {
		*protocol = STATSRELAY_PROTO_CARBON;
		return 0;
	}
	return 1;
}

static void print_help(const char *argv0) {
	fprintf(stderr,
		"Usage: %s [options]                                    \n"
		"    --help                  Display this message\n"
		"    --verbose               Write log messages to stderr in addition to syslog\n"
		"    syslog\n"
		"    --protocol=proto        Set mode as one of: statsd, carbon\n"
		"    (default: %s)\n"
		"    --bind=address[:port]   Bind to the given address and port\n"
		"    (default: *:%s or *:%s)\n"
		"    --config=filename       Use the given hashring config file\n"
		"    (default: %s or %s)\n"
		"    --max-send-queue=BYTES  Limit each backend connection's send queue to\n"
		"    the given size. (default: %" PRIu64 ")\n"
		"    --no-validation         Disable parsing of stat values. Relayed metrics\n"
		"    may not actually be valid past the ':' character\n"
		"    (default: validation is enabled)                \n",
		argv0,
		default_protocol,
		default_statsd_port, default_carbon_port,
		default_statsd_config, default_carbon_config,
		default_max_send_queue);
}

int main(int argc, char **argv) {
	enum statsrelay_proto protocol = STATSRELAY_PROTO_UNKNOWN;
	ev_signal sigint_watcher, sigterm_watcher, sighup_watcher;
	statsrelay_options_t options;
	char *address = NULL;
	char *err;
	int option_index = 0;
	char c = 0;

	stats_log(PACKAGE_STRING);

	options.filename = NULL;
	options.verbose = 0;
	options.max_send_queue = default_max_send_queue;
	options.validate_lines = 1;

	while (c != -1) {
		c = getopt_long(argc, argv, "c:b:p:vh", long_options, &option_index);
		switch (c) {
		case -1:
			break;
		case 0:
		case 'h':
			print_help(argv[0]);
			return 1;
		case 'b':
			address = strdup(optarg);
			break;
		case 'v':
			options.verbose = 1;
			break;
		case 'c':
			if (access(optarg, R_OK)) {
				stats_log("can't read config file at \"%s\"", optarg);
				goto err;
			}
			options.filename = optarg;
			break;
		case 'q':
			options.max_send_queue = strtoull(optarg, &err, 10);
			break;
		case 'n':
			options.validate_lines = 0;
			break;
		case 'p':
			if (choose_protocol(&protocol, optarg)) {
				goto err;
			}
			break;
		default:
			stats_log("main: Unknown argument %c", c);
			goto err;
		}
	}
	if (protocol == STATSRELAY_PROTO_UNKNOWN) {
		// no --proto was passed via argv
		if (choose_protocol(&protocol, default_protocol)) {
			goto err;
		}
	}
	assert(protocol == STATSRELAY_PROTO_STATSD || protocol == STATSRELAY_PROTO_CARBON);
	if (options.filename == NULL) {
		options.filename = \
			protocol == STATSRELAY_PROTO_STATSD ? default_statsd_config : default_carbon_config;
	}

	if (address == NULL) {
		address = malloc(2);
		memcpy(address, "*\0", 2);
	}

	loop = ev_default_loop(0);

	ev_signal_init(&sigint_watcher, graceful_shutdown, SIGINT);
	ev_signal_start(loop, &sigint_watcher);

	ev_signal_init(&sigterm_watcher, graceful_shutdown, SIGTERM);
	ev_signal_start(loop, &sigterm_watcher);

	ev_signal_init(&sighup_watcher, reload_config, SIGHUP);
	ev_signal_start(loop, &sighup_watcher);

	switch (protocol) {
	case STATSRELAY_PROTO_STATSD:
		server = stats_server_create(options.filename, loop, protocol_parser_statsd, validate_statsd);
		break;
	case STATSRELAY_PROTO_CARBON:
		server = stats_server_create(options.filename, loop, protocol_parser_carbon, validate_carbon);
		break;
	default:
		stats_log("main: unknown protocol!\n");
		goto err;
	}

	if (server == NULL) {
		stats_log("main: Unable to create stats_server");
		goto err;
	}
	if (stats_num_backends(server) == 0) {
		stats_log("main: unable to initialize backends from hashring; is \"%s\" a valid config file?", options.filename);
		goto err;
	}

	ts = tcpserver_create(loop, server);
	if (ts == NULL) {
		stats_log("main: Unable to create tcpserver");
		goto err;
	}

	us = udpserver_create(loop, server);
	if (us == NULL) {
		stats_log("main: Unable to create udpserver");
		goto err;
	}

	const char *default_port =\
		protocol == STATSRELAY_PROTO_STATSD ? default_statsd_port : default_carbon_port;
	if (tcpserver_bind(ts, address, default_port, stats_connection, stats_recv) != 0) {
		stats_log("main: Unable to bind tcp %s", address);
		goto err;
	}
	if (udpserver_bind(us, address, default_port, stats_udp_recv) != 0) {
		stats_log("main: Unable to bind udp %s", address);
		goto err;
	}

	stats_log_verbose(options.verbose);
	stats_set_max_send_queue(server, options.max_send_queue);
	stats_set_validate_lines(server, options.validate_lines);

	stats_log("main: Starting event loop");
	ev_run(loop, 0);

	stats_log_end();
	free(address);
	return 0;

err:
	stats_log_end();
	free(address);
	return 1;
}
