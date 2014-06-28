#include "list.h"
#include <stdlib.h>
#include <stdio.h>

int list_create(list_t *list) {
	list = (list_t *)calloc(sizeof(list_t), 1);
	if(list == NULL) {
		return 1;
	}
	list->value = NULL;
	list->next = NULL;
	return 0;
}

list_t *list_append(list_t *list, void *value) {
	list_t *node;
	node = (list_t *)calloc(sizeof(list_t), 1);
	node->value = value;
	node->next = list->next;
	list->next = node;
	return node;
}

list_t *list_next(list_t *list) {
	return list->next;
}

void list_destroy(list_t *list) {
	list_t *node = list->next;

	while(node != NULL) {
		free(list);
		list = node;
		node = list->next;
	}
	free(list);
}

void *list_value(list_t *list) {
	return list->value;
}

void list_print(list_t *list) {
	int i = 0;
	list_t *node = list;

	while(node != NULL) {
		printf("%i: %p\n", i, node->value);
		i++;
		node = list->next;
	}
}
