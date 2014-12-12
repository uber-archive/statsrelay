#include "./hashring.h"

#include "./hashlib.h"
#include "./log.h"

#include <inttypes.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>


struct hashring {
	size_t size;
	void **backends;
	hashring_dealloc_func dealloc;
};

hashring_t hashring_init(const char *hashfile,
			 size_t expected_size,
			 void *alloc_data,
			 hashring_alloc_func alloc,
			 hashring_dealloc_func dealloc) {
	FILE *hash_file;
	if ((hash_file = fopen(hashfile, "r")) == NULL) {
		return NULL;
	}

	struct hashring *ring = malloc(sizeof(struct hashring));
	ring->backends = NULL;
	ring->size = 0;
	ring->dealloc = dealloc;

	char *line = NULL;
	size_t len = 0;
	ssize_t read;

	if (expected_size) {
		if ((ring->backends = malloc(sizeof(void *) * expected_size)) == NULL) {
			goto init_err;
		}
	}
	while ((read = getline(&line, &len, hash_file)) != -1) {
		// strip the trailing newline
		if (line[read] == '\n') {
			line[read] = '\0';
		} else if (read && line[read - 1] == '\n') {
			line[read - 1] = '\0';
		}

		// expand the hashing
		void *obj = alloc(line, alloc_data);
		if (obj == NULL) {
			stats_log("hashring: failed to alloc after reading line \"%s\"", line);
			goto init_err;
		}
		if (!expected_size) {
			void *new_backends = realloc(
				ring->backends, sizeof(void *) * (ring->size + 1));
			if (new_backends == NULL) {
				goto init_err;
			}
			ring->backends = new_backends;
		}
		ring->backends[ring->size++] = obj;
	}

	if (expected_size && ring->size != expected_size) {
		stats_log("hashring: fatal error in init, expected %d lines but actually saw %d lines",
			  expected_size, ring->size);
		goto init_err;
	}

	free(line);
	fclose(hash_file);
	return ring;

init_err:
	free(line);
	fclose(hash_file);
	hashring_dealloc(ring);
	return NULL;
}

size_t hashring_size(hashring_t ring) {
	if (ring == NULL) {
		return 0;
	}
	return ring->size;
}

void* hashring_choose(struct hashring *ring,
		      const char *key,
		      size_t len) {
	if (ring == NULL || ring->size == 0) {
		stats_log("trying to choose from an empty hashring!");
		return NULL;
	}
	const uint32_t index = stats_hash(key, (uint32_t) len, ring->size);
	return ring->backends[index];
}

void hashring_dealloc(struct hashring *ring) {
	if (ring == NULL) {
		return;
	}
	if (ring->backends == NULL) {
		return;
	}
	for (size_t i = 0; i < ring->size; i++) {
		bool need_dealloc = true;
		for (size_t j = 0; j < i; j++) {
			if (ring->backends[i] == ring->backends[j]) {
				need_dealloc = false;
				break;
			}
		}
		if (need_dealloc) {
			ring->dealloc(ring->backends[i]);
		}
	}
	free(ring->backends);
	ring->backends = NULL;
	ring->size = 0;
	free(ring);
}
