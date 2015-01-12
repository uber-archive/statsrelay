#include "list.h"

#include <stdio.h>


list_t statsrelay_list_new() {
	list_t list = malloc(sizeof(struct statsrelay_list));
	if (list == NULL) {
		return list;
	}
	list->data = NULL;
	list->allocated_size = 0;
	list->size = 0;
	return list;
}

void* statsrelay_list_expand(list_t list) {
	size_t index = list->size;
	list->size++;

	if (list->allocated_size < list->size) {
		if (list->allocated_size == 0) {
			list->allocated_size = 1;
			list->data = malloc(sizeof(void *));
			if (list->data == NULL) {
				return NULL;
			}
		} else {
			list->allocated_size <<= 1;
			void *newdata = realloc(
				list->data, sizeof(void *) * list->allocated_size);
			if (newdata == NULL) {
				perror("realloc()");
				return NULL;
			}
			list->data = newdata;
		}
	}
	return list->data + index;
}

void statsrelay_list_destroy(list_t list) {
	free(list->data);
	free(list);
}

void statsrelay_list_destroy_full(list_t list) {
	for (size_t i = 0; i < list->size; i++) {
		free(list->data[i]);
	}
	statsrelay_list_destroy(list);
}
