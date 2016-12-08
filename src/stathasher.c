#include <ctype.h>
#include <getopt.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "./hashring.h"
#include "./yaml_config.h"

static struct option long_options[] = {
	{"config",		required_argument,	NULL, 'c'},
	{"help",		no_argument,		NULL, 'h'},
};

static void* my_strdup(const char *str, void *unused_data) {
	return strdup(str);
}

static void print_help(const char *argv0) {
	printf("Usage: %s [-h] [-c /path/to/config.yaml]", argv0);
}

int main(int argc, char **argv) {
	char *config_name = (char *) default_config;
	int8_t c = 0;
	while (c != -1) {
		c = (int8_t)getopt_long(argc, argv, "c:h", long_options, NULL);
		switch (c) {
		case -1:
			break;
		case 0:
		case 'h':
			print_help(argv[0]);
			return 0;
		case 'c':
			config_name = optarg;
			break;
		default:
			printf("%s: Unknown argument %c\n", argv[0], c);
			return 1;
		}
	}
	if (optind != 1 && optind != 3) {
		printf("%s: unexpected command optoins\n", argv[0]);
		return 1;
	}

	FILE *config_file = fopen(config_name, "r");
	if (config_file == NULL) {
		fprintf(stderr, "failed to open %s\n", config_name);
		return 1;
	}
	struct config *app_cfg = parse_config(config_file);

	fclose(config_file);
	if (app_cfg == NULL) {
		fprintf(stderr, "failed to parse config %s\n", config_name);
		return 1;
	}

	hashring_t carbon_ring = NULL, statsd_ring = NULL;

	if (app_cfg->carbon_config.initialized) {
		carbon_ring = hashring_load_from_config(
			&app_cfg->carbon_config, NULL, my_strdup, free);
	}
	if (app_cfg->statsd_config.initialized) {
		statsd_ring = hashring_load_from_config(
			&app_cfg->statsd_config, NULL, my_strdup, free);
	}
	destroy_config(app_cfg);

	uint32_t shard;
	char *choice = NULL;
	char *line = NULL;
	size_t len;
	ssize_t bytes_read;
	while ((bytes_read = getline(&line, &len, stdin)) != -1) {
		// trim whitespace
		for (ssize_t i = 0; i < bytes_read; i++) {
			if (isspace(line[i])) {
				line[i] = '\0';
				break;
			}
		}
		printf("key=%s", line);
		if (carbon_ring != NULL) {
			choice = hashring_choose(carbon_ring, line, &shard);
			if (choice != NULL) {
				printf(" carbon=%s carbon_shard=%d", choice, shard);
			}
		}
		if (statsd_ring != NULL) {
			choice = hashring_choose(statsd_ring, line, &shard);
			if (choice != NULL) {
				printf(" statsd=%s statsd_shard=%d", choice, shard);
			}
		}
		putchar('\n');
		fflush(stdout);
	}
	free(line);
	hashring_dealloc(carbon_ring);
	hashring_dealloc(statsd_ring);
	return 0;
}
