#include "config.h"
#include "tcpserver.h"
#include "udpserver.h"
#include "stats.h"
#include "log.h"

#include <signal.h>
#include <string.h>
#include <unistd.h>
#include <stdlib.h>
#include <stdio.h>
#include <getopt.h>
#include <glib.h>
#include <ev.h>


static struct option long_options[] = {
	{"config",			required_argument,	NULL, 'c'},
	{"bind",			required_argument,	NULL, 'b'},
	{"verbose",			no_argument,		NULL, 'v'},
	{"help",			no_argument,		NULL, 'h'},
	{"max-send-queue",	required_argument,	NULL, 'q'},
	{"no-validation",	no_argument,		NULL, 'n'},
};

typedef struct statsrelay_options_t {
	GList *binds;
	char *filename;
	int verbose;
	uint64_t max_send_queue;
	int validate_lines;
} statsrelay_options_t;

static stats_server_t *server = NULL;
static tcpserver_t *ts = NULL;
static udpserver_t *us = NULL;
static struct ev_loop *loop = NULL;

static const char default_port[] = "8125";
static const char default_config[] = "/etc/statsrelay.conf";
static const uint64_t default_max_send_queue = 134217728;

void graceful_shutdown(struct ev_loop *loop, ev_signal *w, int revents) {
	//stats_log("Received %s (signal %i), shutting down.", strsignal(signum), signum);
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

void reload_config(struct ev_loop *loop, ev_signal *w, int revents) {
	stats_log("Received SIGHUP, reloading.");
	if (server != NULL) {
		stats_server_reload(server);
	}
}

void print_help(const char *argv0) {
	fprintf(stderr, "Usage: %s [options]                                    \n\
    --help                  Display this message                            \n\
    --verbose               Write log messages to stderr in addition to     \n\
                            syslog                                          \n\
    --bind=address[:port]   Bind to the given address and port              \n\
                            (default: *:%s)                                 \n\
    --config=filename       Use the given ketama config file                \n\
                            (default: %s)                                   \n\
    --max-send-queue=BYTES  Limit each backend connection's send queue to   \n\
                            the given size. (default: %llu)                 \n\
    --no-validation         Disable parsing of stat values. Relayed metrics \n\
                            may not actually be valid past the ':' character\n\
                            (default: validation is enabled)                \n",
		argv0, default_port, default_config, (unsigned long long) default_max_send_queue);
}

int main(int argc, char **argv) {
	ev_signal sigint_watcher, sigterm_watcher, sighup_watcher;
	statsrelay_options_t options;
	char *address, *err;
	GList *l = NULL;
	size_t len;
	int option_index = 0;
	char c = 0;

	options.binds = NULL;
	options.verbose = 0;
	options.max_send_queue = default_max_send_queue;
	options.validate_lines = 1;
	if ((options.filename = strdup(default_config)) == NULL) {
		fprintf(stderr, "main: failed to strdup(3)\n");
		return 1;
	}

	stats_log(PACKAGE_STRING);

	while (c != -1) {
		c = getopt_long(argc, argv, "c:b:vh", long_options, &option_index);

		switch (c) {
			case -1:
				break;
			case 0:
			case 'h':
				print_help(argv[0]);
				return 1;
			case 'b':
				len = strlen(optarg) + 1;
				address = malloc(len);
				memcpy(address, optarg, len);
				options.binds = g_list_prepend(options.binds, address);
				break;
			case 'v':
				options.verbose = 1;
				break;
			case 'c':
				options.filename = optarg;
				break;
			case 'q':
				options.max_send_queue = strtoull(optarg, &err, 10);
				break;
			case 'n':
				options.validate_lines = 0;
				break;
			default:
				stats_log("main: Unknown argument %c", c);
				return 3;
		}
	}

	if (options.binds == NULL) {
		address = malloc(2);
		memcpy(address, "*\0", 2);
		options.binds = g_list_prepend(options.binds, address);
	}

	loop = ev_default_loop(0);

	ev_signal_init(&sigint_watcher, graceful_shutdown, SIGINT);
	ev_signal_start(loop, &sigint_watcher);

	ev_signal_init(&sigterm_watcher, graceful_shutdown, SIGTERM);
	ev_signal_start(loop, &sigterm_watcher);

	ev_signal_init(&sighup_watcher, reload_config, SIGHUP);
	ev_signal_start(loop, &sighup_watcher);

	server = stats_server_create(options.filename, loop);

	if (server == NULL) {
		stats_log("main: Unable to create stats_server");
		g_list_free_full(options.binds, free);
		return 1;
	}

	ts = tcpserver_create(loop, server);
	if (ts == NULL) {
		stats_log("main: Unable to create tcpserver");
		g_list_free_full(options.binds, free);
		return 3;
	}

	us = udpserver_create(loop, server);
	if (us == NULL) {
		stats_log("main: Unable to create udpserver");
		g_list_free_full(options.binds, free);
		return 5;
	}

	for (l = options.binds; l != NULL; l = l->next) {
		address = l->data;
		if (tcpserver_bind(ts, address, default_port, stats_connection, stats_recv) != 0) {
			stats_log("main: Unable to bind tcp %s", address);
			g_list_free_full(options.binds, free);
			return 6;
		}

		if (udpserver_bind(us, address, default_port, stats_udp_recv) != 0) {
			stats_log("main: Unable to bind udp %s", address);
			g_list_free_full(options.binds, free);
			return 7;
		}
	}


	stats_log_verbose(options.verbose);
	stats_set_max_send_queue(server, options.max_send_queue);
	stats_set_validate_lines(server, options.validate_lines);

	stats_log("main: Starting event loop");
	ev_run(loop, 0);

	g_list_free_full(options.binds, free);
	return 0;
}
