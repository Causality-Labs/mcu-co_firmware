#include <string.h>
#include <stddef.h>
#include "ring-buffer.h"

int ring_buffer_init(ring_buffer_t *rb, void *buffer, uint16_t capacity, size_t element_size)
{
    if (rb == NULL || buffer == NULL) {
        return -1;
    }

    if (element_size == 0U) {
        return -1;
    }

    if ((capacity == 0U) || ((capacity & (capacity - 1U)) != 0U)) {
        return -1;
    }

    rb->buffer       = buffer;
    rb->element_size = element_size;
    rb->capacity     = capacity;
    rb->head         = 0U;
    rb->tail         = 0U;
    rb->mask         = (uint16_t)(capacity - 1U);

    return 0;
}

bool ring_buffer_is_empty(const ring_buffer_t *rb)
{
    if (rb == NULL) {
        return -1;
    }

    return rb->head == rb->tail;
}

bool ring_buffer_is_full(const ring_buffer_t *rb)
{
    if (rb == NULL) {
        return -1;
    }

    return ((rb->head + 1U) & rb->mask) == rb->tail;
}

int ring_buffer_write(ring_buffer_t *rb, const void *element)
{
    if (rb == NULL || element == NULL) {
        return -1;
    }

    if (ring_buffer_is_full(rb)) {
        return -1;
    }

    uint8_t *dst = (uint8_t *)rb->buffer + (rb->head * rb->element_size);
    (void)memcpy(dst, element, rb->element_size);
    rb->head = (uint16_t)((rb->head + 1U) & rb->mask);

    return 0;
}

int ring_buffer_read(ring_buffer_t *rb, void *element)
{
    if (rb == NULL || element == NULL) {
        return -1;
    }

    if (ring_buffer_is_empty(rb)) {
        return -1;
    }

    const uint8_t *src = (const uint8_t *)rb->buffer + (rb->tail * rb->element_size);
    (void)memcpy(element, src, rb->element_size);
    rb->tail = (uint16_t)((rb->tail + 1U) & rb->mask);

    return 0;
}

int ring_buffer_flush(ring_buffer_t *rb)
{
    if (rb == NULL) {
        return -1;
    }

    rb->head = 0U;
    rb->tail = 0U;

    return 0;
}
