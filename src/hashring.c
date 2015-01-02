#include "./hashring.h"

#include "./hashlib.h"
#include "./list.h"
#include "./log.h"

#include <inttypes.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

struct hashring {
	list_t backends;
	void *alloc_data;
	hashring_alloc_func alloc;
	hashring_dealloc_func dealloc;
};

hashring_t hashring_init(void *alloc_data,
			 hashring_alloc_func alloc,
			 hashring_dealloc_func dealloc) {
	struct hashring *ring = malloc(sizeof(struct hashring));
	if (ring == NULL) {
		stats_error_log("failure to malloc() in hashring_init");
		return NULL;
	}
	ring->backends = statsrelay_list_new();
	ring->alloc_data = alloc_data;
	ring->alloc = alloc;
	ring->dealloc = dealloc;
	return ring;
}

hashring_t hashring_load_from_config(struct proto_config *pc,
				     void *alloc_data,
				     hashring_alloc_func alloc_func,
				     hashring_dealloc_func dealloc_func) {
	hashring_t ring = hashring_init(alloc_data, alloc_func, dealloc_func);
	if (ring == NULL) {
		stats_error_log("failed to hashring_init");
		return NULL;
	}
	for (size_t i = 0; i < pc->ring->size; i++) {
		if (!hashring_add(ring, pc->ring->data[i])) {
			hashring_dealloc(ring);
			return NULL;
		}
	}
	return ring;
}

bool hashring_add(hashring_t ring, const char *line) {
	if (line == NULL) {
		stats_error_log("cowardly refusing to alloc NULL pointer");
		goto add_err;
	}
	// allocate an object
	void *obj = ring->alloc(line, ring->alloc_data);
	if (obj == NULL) {
		stats_error_log("hashring: failed to alloc line \"%s\"", line);
		goto add_err;
	}

	// grow the list
	if (statsrelay_list_expand(ring->backends) == NULL) {
		stats_error_log("hashring: failed to expand list");
		ring->dealloc(obj);
		goto add_err;
	}

	ring->backends->data[ring->backends->size - 1] = obj;
	return true;

add_err:
	return false;
}

size_t hashring_size(hashring_t ring) {
	if (ring == NULL) {
		return 0;
	}
	return ring->backends->size;
}

void* hashring_choose(struct hashring *ring,
		      const char *key,
		      uint32_t *shard_num) {
	if (ring == NULL) {
		return NULL;
	}

	const size_t ring_size = ring->backends->size;

	if (ring_size == 0) {
		return NULL;
	}

	const uint32_t index = stats_hash(key, strlen(key), ring_size);
	if (shard_num != NULL) {
		*shard_num = index;
	}
	return ring->backends->data[index];
}

void hashring_dealloc(struct hashring *ring) {
	if (ring == NULL) {
		return;
	}
	if (ring->backends == NULL) {
		return;
	}
	const size_t ring_size = ring->backends->size;
	for (size_t i = 0; i < ring_size; i++) {
		bool need_dealloc = true;
		for (size_t j = 0; j < i; j++) {
			if (ring->backends->data[i] == ring->backends->data[j]) {
				need_dealloc = false;
				break;
			}
		}
		if (need_dealloc) {
			ring->dealloc(ring->backends->data[i]);
		}
	}
	statsrelay_list_destroy(ring->backends);
	free(ring);
}
