#include <stdlib.h>
#include <string.h>

#include "buffer.h"

#define INITIAL_BUFFER_SIZE 4096


/*
   ptr          head                  tail
    |            |XXXXXXXXXXXXXXXXXXXXXX|              |
    [ ---------------- size ---------------------------]
                                        [ spacecount   ]
                 [     datacount        ]

*/

int buffer_allocate(buffer_t *b, size_t size)
{
    b->size = size;
    b->ptr = (char *)malloc(b->size);
    if (!b->ptr) return -1;
#ifdef SANITIZE_BUFFERS
	memset(b->ptr, 0, size);
#endif
    b->head = b->ptr;
    b->tail = b->ptr;
    return 0;
}

int buffer_init(buffer_t *b)
{
    return buffer_allocate(b, INITIAL_BUFFER_SIZE);
}

int buffer_init_contents(buffer_t *b, const char *data, size_t size)
{
    buffer_allocate(b, size);
    return buffer_set(b, data, size);
}

buffer_t *create_buffer(size_t size)
{
    buffer_t *ret = (buffer_t *)malloc(sizeof(*ret));
    if (0 != buffer_allocate(ret, size)) {
        free(ret);
        ret = NULL;
    }
    return ret;
}

size_t buffer_datacount(buffer_t *b)
{
    return b->tail - b->head;
}

size_t buffer_spacecount(buffer_t *b)
{
    return b->size - (b->tail - b->ptr);
}

char *buffer_head(buffer_t *b)
{
    return b->head;
}

char *buffer_tail(buffer_t *b)
{
    return b->tail;
}

/* Assumes we are always making it bigger */
char *myrealloc(char *p, size_t old, size_t new)
{
    char *pnew = malloc(new);
	if (pnew == NULL) {
		return NULL;
	}
    memcpy(pnew, p, old);
    free(p);
    return pnew;
}

int buffer_newsize(buffer_t *b, size_t newsize)
{
    char *pnew = myrealloc(b->ptr, b->size, newsize);
    if (!pnew)
        return -1;
    b->head = pnew + (b->head - b->ptr);
    b->tail = pnew + (b->tail - b->ptr);
    b->ptr = pnew;
    b->size = newsize;
    return 0;
}

int buffer_expand(buffer_t *b)
{
    return buffer_newsize(b, b->size * 2);
}

int buffer_consume(buffer_t *b, size_t amt)
{
    if (b->head + amt > b->tail)
        return -1;

    b->head += amt;
    return 0;
}

int buffer_produced(buffer_t *b, size_t amt)
{
    if ((b->tail + amt) - b->ptr > b->size)
        return -1;

    b->tail += amt;
    return 0;
}

int buffer_set(buffer_t *b, const char *data, size_t size)
{
    if (b->size < size) {
        if (0 != buffer_newsize(b, size))
            return -1;
    }
    memcpy(b->ptr, data, size);
    return buffer_produced(b, size);
}

int buffer_realign(buffer_t *b)
{
    if (b->tail != b->head) {
        memmove(b->ptr, b->head, b->tail - b->head);
    }
    /* do not switch the order of the following two statements */
    b->tail = b->ptr + (b->tail - b->head);
    b->head = b->ptr;
    return 0;
}

void buffer_destroy(buffer_t *b)
{
    free(b->ptr);
    b->head = NULL;
    b->tail = NULL;
    b->size = 0;
}

void delete_buffer(buffer_t *b)
{
    buffer_destroy(b);
    free(b);
}

void buffer_wrap(buffer_t *b, const char *data, size_t size)
{
    /* Promise not to modify */
    b->ptr = (char *)data;
    b->head = b->ptr;
    b->tail = b->head + size;
    b->size = 0;
}
