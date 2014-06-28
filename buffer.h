#ifndef BUFFER_H
#define BUFFER_H

#include <sys/types.h>

struct buffer {
    unsigned char *ptr;
    unsigned char *head;
    unsigned char *tail;
    size_t size;
};

typedef struct buffer buffer_t;

// Init a buffer to default size
int buffer_init(buffer_t *);
int buffer_init_contents(buffer_t *, const char *, size_t);
// Create a new buffer at specified size
buffer_t *create_buffer(size_t size);

// Returns the size of the consumed space in buffer
size_t buffer_datacount(buffer_t *);

// Returns the size of available space in buffer
size_t buffer_spacecount(buffer_t *);

// Returns a pointer to the beginning of used space
unsigned char *buffer_head(buffer_t *);

// Returns a pointer to the end of used space
unsigned char *buffer_tail(buffer_t *);

// Doubles the size of the buffer
int buffer_expand(buffer_t *);

// Advances head
int buffer_consume(buffer_t *, size_t);

// Advances tail
int buffer_produced(buffer_t *, size_t);

// Sets to the new contents, expanding if necessary
int buffer_set(buffer_t *, const char *data, size_t size);

// Copy data from head to the beginning of the buffer
int buffer_realign(buffer_t *);

// Frees all memory associated with the buffer
void buffer_destroy(buffer_t *);
// Delete the buffer object
void delete_buffer(buffer_t *);

/* Take this piece of memory and wrap it. Don't copy it, don't touch it,
   just wrap it. This is entirely for interface compatibilty with things
   that take buffers */
void buffer_wrap(buffer_t *, const char *, size_t);

#endif
