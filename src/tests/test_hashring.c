#include "../hashring.h"
#include "../log.h"

#include <assert.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>


static void *my_strdup(const char *str, void *data) {
	return strdup(str);
}


int main(int argc, char **argv) {
	// Test the hashring.  Note that when the hash space is
	// expanded in hashring1 -> hashring2, we are checking
	// explicitly that apple and orange do not move to new nodes.
	stats_log_verbose(1);

	hashring_t ring = hashring_init("tests/hashring1.txt", 4, NULL, my_strdup, free);
	assert(ring != NULL);
	assert(strcmp(hashring_choose(ring, "apple", strlen("apple")), "127.0.0.1:9001") == 0);
	assert(strcmp(hashring_choose(ring, "banana", strlen("banana")), "127.0.0.1:9001") == 0);
	assert(strcmp(hashring_choose(ring, "orange", strlen("orange")), "127.0.0.1:9000") == 0);
	assert(strcmp(hashring_choose(ring, "lemon", strlen("lemon")), "127.0.0.1:9000") == 0);
	hashring_dealloc(ring);

	ring = hashring_init("tests/hashring2.txt", 4, NULL, my_strdup, free);
	assert(ring != NULL);
	assert(strcmp(hashring_choose(ring, "apple", strlen("apple")), "127.0.0.1:9001") == 0);
	assert(strcmp(hashring_choose(ring, "banana", strlen("banana")), "127.0.0.1:9003") == 0);
	assert(strcmp(hashring_choose(ring, "orange", strlen("orange")), "127.0.0.1:9000") == 0);
	assert(strcmp(hashring_choose(ring, "lemon", strlen("lemon")), "127.0.0.1:9002") == 0);
	hashring_dealloc(ring);

	return 0;
}
