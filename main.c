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
};

typedef struct statsrelay_options_t {
	GList *binds;
	char *filename;
	int verbose;
} statsrelay_options_t;

typedef struct statsrelay_address_t {
	char *host;
	char *port;
} statsrelay_address_t;


stats_server_t *server = NULL;
tcpserver_t *ts = NULL;
udpserver_t *us = NULL;
struct ev_loop *loop = NULL;


void graceful_shutdown(struct ev_loop *loop, ev_signal *w, int revents) {
	//stats_log("Received %s (signal %i), shutting down.", strsignal(signum), signum);
	stats_log("Received signal, shutting down.");
	ev_break(loop, EVBREAK_ALL);

	if(ts != NULL) {
		tcpserver_destroy(ts);
	}
	if(us != NULL) {
		udpserver_destroy(us);
	}
	if(server != NULL) {
		stats_server_destroy(server);
	}
}

void reload_config(struct ev_loop *loop, ev_signal *w, int revents) {
	stats_log("Received SIGHUP, reloading.");
	if(server != NULL) {
		stats_server_reload(server);
	}
}

void print_help(const char *argv0) {
	fprintf(stderr, "%s [options]                                           \n\
    --help                  Display this message                            \n\
    --verbose               Write log messages to stderr in addition to     \n\
                            syslog                                          \n\
    --bind=address[:port]   Bind to the given address and port              \n\
                            (default: *:8125)                               \n\
    --config=filename       Use the given ketama config file                \n\
                            (default: /etc/statsrelay.conf)                 \n",
		argv0);
}

statsrelay_address_t *parse_bind_argument(char *optarg) {
	statsrelay_address_t *address;
	char *ptr;
	size_t len;

	len = strlen(optarg);

	address = malloc(sizeof(statsrelay_address_t));
	address->host = optarg;

	ptr = memchr(optarg, ':', len);
	if(ptr == NULL) {
		address->port = "8125";
	}else{
		ptr[0] = '\0';
		address->port = ptr + 1;
	}
	return address;
}

int main(int argc, char **argv) {
	ev_signal sigint_watcher, sigterm_watcher, sighup_watcher;
	statsrelay_options_t options;
	statsrelay_address_t *address;
	GList *l;
	int option_index = 0;
	char c = 0;

	options.binds = NULL;
	options.filename = "/etc/statsrelay.conf";
	options.verbose = 0;

	while(c != -1) {
		c = getopt_long(argc, argv, "c:b:vh", long_options, &option_index);

		switch(c) {
			case -1:
				break;
			case 0:
			case 'h':
				print_help(argv[0]);
				return 1;
			case 'b':
				address = parse_bind_argument(optarg);
				if(address == NULL) {
					stats_log("main: Unable to parse bind argument %s", optarg);
					return 2;
				}
				options.binds = g_list_prepend(options.binds, address);
				break;
			case 'v':
				options.verbose = 1;
				break;
			case 'c':
				options.filename = optarg;
				break;
			default:
				stats_log("main: Unknown argument %s", c);
				return 3;
		}
	}

	if(options.binds == NULL) {
		address = malloc(sizeof(statsrelay_address_t));
		address->host = "*";
		address->port = "8125";
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

	if(server == NULL) {
		stats_log("main: Unable to create stats_server");
		return 1;
	}

	ts = tcpserver_create(loop, server);
	if(ts == NULL) {
		stats_log("main: Unable to create tcpserver");
		return 3;
	}

	us = udpserver_create(loop, server);
	if(us == NULL) {
		stats_log("main: Unable to create udpserver");
		return 5;
	}

	for (l = options.binds; l != NULL; l = l->next) {
		address = l->data;
		if(tcpserver_bind(ts, address->host, address->port, stats_connection, stats_recv) != 0) {
			stats_log("main: Unable to bind tcp %s:%s", address->host, address->port);
			return 6;
		}

		if(udpserver_bind(us, address->host, address->port, stats_udp_recv) != 0) {
			stats_log("main: Unable to bind udp %s:%s", address->host, address->port);
			return 7;
		}
	}

	stats_log_verbose(options.verbose);

	stats_log("main: Starting event loop");
	ev_run(loop, 0);

	for (l = options.binds; l != NULL; l = l->next) {
		address = (statsrelay_address_t *)l->data;
		free(address);
	}

	return 0;
}
