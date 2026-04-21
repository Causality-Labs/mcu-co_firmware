#ifndef RING_BUFFER_H
#define RING_BUFFER_H

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>

typedef struct {
    void     *buffer;
    size_t    element_size;
    uint16_t  capacity;
    uint16_t  head;
    uint16_t  tail;
    uint16_t  mask;
} ring_buffer_t;

int  ring_buffer_init (ring_buffer_t *rb, void *buffer, uint16_t capacity, size_t element_size);
int  ring_buffer_write(ring_buffer_t *rb, const void *element);
int  ring_buffer_read (ring_buffer_t *rb, void *element);
int  ring_buffer_flush(ring_buffer_t *rb);

bool ring_buffer_is_empty(const ring_buffer_t *rb);
bool ring_buffer_is_full (const ring_buffer_t *rb);

#define rb_write(rb, val) ring_buffer_write((rb), &(val))
#define rb_read(rb, val)  ring_buffer_read((rb),  &(val))

#endif /* RING_BUFFER_H */
