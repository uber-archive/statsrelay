#ifndef LIST_H
#define LIST_H

struct list_t {
	void *value;
	struct list_t *next;
};
typedef struct list_t list_t;

int list_create(list_t *list);
list_t *list_append(list_t *list, void *value);
list_t *list_next(list_t *list);
void *list_value(list_t *list);
void list_destroy(list_t *list);
void list_print(list_t *list);

#endif
