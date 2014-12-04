#include "config.h"
#include "stats.h"
#include "log.h"

#include <signal.h>
#include <string.h>
#include <unistd.h>
#include <stdlib.h>
#include <stdio.h>
#include <getopt.h>
#include <glib.h>
#include <stdint.h>


static struct option long_options[] = {
	{"config",			required_argument,	NULL, 'c'},
	{"verbose",			no_argument,		NULL, 'v'},
	{"help",			no_argument,		NULL, 'h'},
};

typedef struct statsrelay_options_t {
	char *filename;
	int verbose;
} statsrelay_options_t;

struct stats_server_t {
	char *ketama_filename;
	ketama_continuum kc;
	GHashTable *backends;
	GHashTable *ketama_cache;
	struct ev_loop *loop;

	uint64_t max_send_queue;
	int validate_lines;

	uint64_t bytes_recv_udp;
	uint64_t bytes_recv_tcp;
	uint64_t total_connections;
	uint64_t malformed_lines;
	time_t last_reload;
};

stats_server_t *server = NULL;


void print_help(const char *argv0) {
	fprintf(stderr, "Usage: %s [options] [FILENAME]                         \n\
    --help                  Display this message                            \n\
    --verbose               Write log messages to stderr in addition to     \n\
                            syslog                                          \n\
    --config=filename       Use the given ketama config file                \n\
                            (default: /etc/statsrelay.conf)                 \n",
		argv0);
}

void print_ip(stats_server_t *server, char *key) {
    mcs *ks;
    ks = ketama_get_server(key, server->kc);
    printf("%s\n", ks->ip);
}

int main(int argc, char **argv) {
	statsrelay_options_t options;
	int option_index = 0;
	char c = 0;
    FILE *input;
    char *lineptr;
    size_t linelen, len;
    mcs *ks;

	options.filename = "/etc/statsrelay.conf";
	options.verbose = 0;

	while(c != -1) {
		c = getopt_long(argc, argv, "c:vh", long_options, &option_index);

		switch(c) {
			case -1:
				break;
			case 0:
			case 'h':
				print_help(argv[0]);
				return 1;
			case 'v':
				options.verbose = 1;
				break;
			case 'c':
				options.filename = optarg;
				break;
			default:
				stats_log("main: Unknown argument %c", c);
				return 3;
		}
	}



	server = stats_server_create(options.filename, NULL);

	if(server == NULL) {
		stats_log("main: Unable to create stats_server");
		return 1;
	}

	stats_log_verbose(options.verbose);

    if (optind >= argc) {
        input = stdin;
    } else {
        input = fopen(argv[optind], "r");
        if (input == NULL) {
            printf("Could not open %s", argv[optind]);
            return 1;
        }
    }

    lineptr = NULL;
    while ((len = getline(&lineptr, &linelen, input)) != -1) {
        lineptr[len-1] = '\0';
        ks = ketama_get_server(lineptr, server->kc);
        printf("%s\n", ks->ip);
        free(lineptr);
        lineptr = NULL;
    }
	return 0;
}
