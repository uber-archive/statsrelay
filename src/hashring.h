#ifndef STATSRELAY_HASHRING_H
#define STATSRELAY_HASHRING_H

#include <stdbool.h>
#include <stddef.h>

#include "./yaml_config.h"

typedef void* (*hashring_alloc_func)(const char *, void *data);
typedef void (*hashring_dealloc_func)(void *);

// opaque hashring type
struct hashring;
typedef struct hashring* hashring_t;

// Initialize the hashring with the list of backends.
hashring_t hashring_init(void *alloc_data,
			 hashring_alloc_func alloc_func,
			 hashring_dealloc_func dealloc_func);


hashring_t hashring_load_from_config(struct proto_config *pc,
				     void *alloc_data,
				     hashring_alloc_func alloc_func,
				     hashring_dealloc_func dealloc_func);

// Add an item to the hashring; returns true on success, false on
// failure.
bool hashring_add(hashring_t ring, const char *line);

// The size of the hashring
size_t hashring_size(hashring_t ring);

// Choose a backend; if shard_num is not NULL, the shard number that
// was used will be placed into the return value.
void *hashring_choose(hashring_t ring,
		      const char *key,
		      uint32_t *shard_num);

// Release allocated memory
void hashring_dealloc(hashring_t ring);

#endif // STATSRELAY_HASHRING_H
