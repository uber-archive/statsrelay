#include "../hashring.h"
#include "../log.h"

#include <assert.h>
#include <ctype.h>
#include <stdlib.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>


static void *my_strdup(const char *str, void *data) {
	return strdup(str);
}

static hashring_t create_ring(const char *filename) {
	hashring_t ring = hashring_init(NULL, my_strdup, free);
	assert(ring != NULL);

	FILE *fp = fopen(filename, "r");
	assert(fp != NULL);
	char *line = NULL;
	size_t len = 0;
	ssize_t read;
	while ((read = getline(&line, &len, fp)) != -1) {
		for (ssize_t i = 0; i < read; i++) {
			if (isspace(line[i])) {
				line[i] = '\0';
				break;
			}
		}
		assert(hashring_add(ring, line));
	}
	fclose(fp);
	free(line);
	return ring;
}

// Test the hashring.  Note that when the hash space is expanded in
// hashring1 -> hashring2, we are checking explicitly that apple and
// orange do not move to new nodes.
int main(int argc, char **argv) {
	stats_log_verbose(1);

	hashring_t ring = create_ring("tests/hashring1.txt");
	assert(ring != NULL);
	uint32_t i;
	assert(strcmp(hashring_choose(ring, "apple", &i), "127.0.0.1:9001") == 0);
	assert(i == 2);
	assert(strcmp(hashring_choose(ring, "banana", &i), "127.0.0.1:9001") == 0);
	assert(i == 3);
	assert(strcmp(hashring_choose(ring, "orange", &i), "127.0.0.1:9000") == 0);
	assert(i == 0);
	assert(strcmp(hashring_choose(ring, "lemon", &i), "127.0.0.1:9000") == 0);
	assert(i == 1);
	hashring_dealloc(ring);

	ring = create_ring("tests/hashring2.txt");
	assert(strcmp(hashring_choose(ring, "apple", &i), "127.0.0.1:9001") == 0);
	assert(i == 2);
	assert(strcmp(hashring_choose(ring, "banana", &i), "127.0.0.1:9003") == 0);
	assert(i == 3);
	assert(strcmp(hashring_choose(ring, "orange", &i), "127.0.0.1:9000") == 0);
	assert(i == 0);
	assert(strcmp(hashring_choose(ring, "lemon", &i), "127.0.0.1:9002") == 0);
	assert(i == 1);
	hashring_dealloc(ring);

	return 0;
}
