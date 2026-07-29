#include <string.h>
#include <stddef.h>
#include "ring-buffer.h"

status_t ring_buffer_init(ring_buffer_t *rb, void *buffer, uint16_t capacity, size_t element_size)
{
    if (rb == NULL || buffer == NULL)
    {
        return STATUS_ERR_INVALID_ARG;
    }

    if (element_size == 0U)
    {
        return STATUS_ERR_INVALID_ARG;
    }

    if ((capacity == 0U) || ((capacity & (capacity - 1U)) != 0U))
    {
        return STATUS_ERR_INVALID_ARG;
    }

    rb->buffer       = buffer;
    rb->element_size = element_size;
    rb->capacity     = capacity;
    rb->head         = 0U;
    rb->tail         = 0U;
    rb->mask         = (uint16_t)(capacity - 1U);

    return STATUS_OK;
}

bool ring_buffer_is_empty(const ring_buffer_t *rb)
{
    if (rb == NULL)
    {
        return false;
    }

    return rb->head == rb->tail;
}

bool ring_buffer_is_full(const ring_buffer_t *rb)
{
    if (rb == NULL)
    {
        return false;
    }

    return ((rb->head + 1U) & rb->mask) == rb->tail;
}

status_t ring_buffer_write(ring_buffer_t *rb, const void *element, bool overwrite)
{
    if (rb == NULL || element == NULL)
    {
        return STATUS_ERR_INVALID_ARG;
    }

    if (ring_buffer_is_full(rb))
    {
        if (!overwrite)
        {
            return STATUS_ERR_FULL;
        }

        rb->tail = (uint16_t)((rb->tail + 1U) & rb->mask);
    }

    uint8_t *dst = (uint8_t *)rb->buffer + (rb->head * rb->element_size);
    (void)memcpy(dst, element, rb->element_size);
    rb->head = (uint16_t)((rb->head + 1U) & rb->mask);

    return STATUS_OK;
}

status_t ring_buffer_read(ring_buffer_t *rb, void *element)
{
    if (rb == NULL || element == NULL)
    {
        return STATUS_ERR_INVALID_ARG;
    }

    if (ring_buffer_is_empty(rb))
    {
        return STATUS_ERR_EMPTY;
    }

    const uint8_t *src = (const uint8_t *)rb->buffer + (rb->tail * rb->element_size);
    (void)memcpy(element, src, rb->element_size);
    rb->tail = (uint16_t)((rb->tail + 1U) & rb->mask);

    return STATUS_OK;
}

status_t ring_buffer_flush(ring_buffer_t *rb)
{
    if (rb == NULL)
    {
        return STATUS_ERR_INVALID_ARG;
    }

    rb->head = 0U;
    rb->tail = 0U;

    return STATUS_OK;
}
