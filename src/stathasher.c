#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>

#include "./hashring.h"
#include "./yaml_config.h"

static void* my_strdup(const char *str, void *unused_data) {
	return strdup(str);
}

int main(int argc, char **argv) {
	if (argc != 2) {
		fprintf(stderr, "usage: %s /path/to/hash.txt\n", argv[0]);
		return 1;
	}

	FILE *config_file = fopen(argv[1], "r");
	if (config_file == NULL) {
		fprintf(stderr, "failed to open %s\n", argv[1]);
		return 1;
	}
	struct config *app_cfg = parse_config(config_file);

	fclose(config_file);
	if (app_cfg == NULL) {
		fprintf(stderr, "failed to parse config %s\n", argv[1]);
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
		choice = hashring_choose(carbon_ring, line, &shard);
		if (choice != NULL) {
			printf(" carbon=%s carbon_shard=%d", choice, shard);
		}
		choice = hashring_choose(statsd_ring, line, &shard);
		if (choice != NULL) {
			printf(" statsd=%s statsd_shard=%d", choice, shard);
		}
		putchar('\n');
	}
	hashring_dealloc(carbon_ring);
	hashring_dealloc(statsd_ring);
	return 0;
}
