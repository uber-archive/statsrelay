#ifndef STATSRELAY_LIST_H
#define STATSRELAY_LIST_H

#include <stdlib.h>

struct statsrelay_list {
	size_t allocated_size;
	size_t size;
	void **data;
};

typedef struct statsrelay_list* list_t;

// create a new list
list_t statsrelay_list_new();

// get the address for a new item in the list, and ensure its size is
// expanded
void *statsrelay_list_expand(list_t list);

// deallocate the list
void statsrelay_list_destroy(list_t list);

// deallocate the list and its contents
void statsrelay_list_destroy_full(list_t list);


#endif  // STATSRELAY_LIST_H
