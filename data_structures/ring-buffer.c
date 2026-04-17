
#include <stddef.h>
#include <stdbool.h>
#include "ring-buffer.h"


int ring_buffer_init(ring_buffer_t *ring_buffer, uint8_t *buffer, uint8_t size)
{
    if (ring_buffer == NULL || buffer == NULL) {
        return -1;
    }

    if ((size == 0U) || ((size & (size - 1U)) != 0U)) {
        return -1;  /* not a power of 2 */
    }

    ring_buffer->buffer = buffer;
    ring_buffer->size = size;
    ring_buffer->head = 0U;
    ring_buffer->tail = 0U;
    ring_buffer->mask = size - 1U;

    return 0;
}

int ring_buffer_flush(ring_buffer_t *ring_buffer)
{

    return 0;
}

bool is_ring_buffer_empty(const ring_buffer_t *ring_buffer)
{
    return ring_buffer->head == ring_buffer->tail;
}

bool is_ring_buffer_full(const ring_buffer_t *ring_buffer)
{
    return ((ring_buffer->head + 1U) & ring_buffer->mask) == ring_buffer->tail;
}

int ring_buffer_write(ring_buffer_t *ring_buffer, uint8_t byte)
{
    if (ring_buffer == NULL) {
        return -1;
    }

    if (is_ring_buffer_full(ring_buffer)) {
        return -1;
    }

    ring_buffer->buffer[ring_buffer->head] = byte;
    ring_buffer->head = (ring_buffer->head + 1U) & ring_buffer->mask;

    return 0;
}

int ring_buffer_read(ring_buffer_t *ring_buffer, uint8_t *byte)
{
    if (ring_buffer == NULL || byte == NULL) {
        return -1;
    }

    if (is_ring_buffer_empty(ring_buffer)) {
        return -1;
    }

    *byte = ring_buffer->buffer[ring_buffer->tail];

    ring_buffer->tail = (ring_buffer->tail + 1U) & ring_buffer->mask;

    return 0;
}
