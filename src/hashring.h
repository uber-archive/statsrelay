#ifndef STATSRELAY_HASHRING_H
#define STATSRELAY_HASHRING_H

#include <stddef.h>

typedef void* (*hashring_alloc_func)(const char *, void *data);
typedef void (*hashring_dealloc_func)(void *);

// opaque hashring type
struct hashring;
typedef struct hashring* hashring_t;

// Initialize the hashring with the list of backends.
hashring_t hashring_init(const char *hashfile,
			 size_t expected_size,
			 void *alloc_data,
			 hashring_alloc_func alloc_func,
			 hashring_dealloc_func dealloc_func);

// The size of the hashring
size_t hashring_size(hashring_t ring);

// Choose a backend
void *hashring_choose(hashring_t ring,
		      const char *key,
		      size_t len);

// Release allocated memory
void hashring_dealloc(hashring_t ring);

#endif // STATSRELAY_HASHRING_H
