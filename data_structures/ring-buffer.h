#ifndef RING_BUFFER_H
#define RING_BUFFER_H

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>
#include "status.h"

/**
 * @brief Generic fixed-capacity ring (circular) buffer.
 *
 * Stores elements of an arbitrary, caller-defined size via memcpy over
 * caller-supplied backing storage. Safe for single-producer/single-consumer
 * use (e.g. an ISR writing, the main loop reading): the producer owns @p head
 * and the consumer owns @p tail.
 *
 * One slot is reserved to distinguish full from empty, so a buffer with
 * @p capacity slots holds at most @p capacity - 1 elements.
 *
 * Do not modify the fields directly; use the ring_buffer_* API.
 */
typedef struct
{
    void *buffer;        /**< Caller-supplied backing storage. */
    size_t element_size; /**< Size in bytes of a single element. */
    uint16_t capacity;   /**< Total slot count; must be a power of two. */
    uint16_t head;       /**< Write index (producer-owned). */
    uint16_t tail;       /**< Read index (consumer-owned). */
    uint16_t mask;       /**< capacity - 1, used for index wraparound. */
} ring_buffer_t;

/**
 * @brief Initialise a ring buffer over caller-supplied storage.
 *
 * @p buffer must remain valid for the lifetime of the ring buffer and be at
 * least @p capacity * @p element_size bytes. @p capacity must be a power of
 * two so index wraparound can use a bitmask.
 *
 * @param rb           Ring buffer to initialise
 * @param buffer       Backing storage for @p capacity elements
 * @param capacity     Number of slots; must be a non-zero power of two
 * @param element_size Size in bytes of a single element; must be non-zero
 * @return STATUS_OK on success, STATUS_ERR_INVALID_ARG on a NULL pointer, zero
 *         @p element_size, or a @p capacity that is zero or not a power of two.
 */
status_t ring_buffer_init(ring_buffer_t *rb, void *buffer, uint16_t capacity, size_t element_size);

/**
 * @brief Copy one element into the buffer.
 *
 * Copies @p element_size bytes from @p element into the next free slot and
 * advances the head. Fails if the buffer is full; existing data is never
 * overwritten.
 *
 * @param rb      Ring buffer to write to
 * @param element Pointer to the element to copy in
 * @return STATUS_OK on success, STATUS_ERR_INVALID_ARG on a NULL pointer,
 *         STATUS_ERR_FULL if the buffer is full.
 */
status_t ring_buffer_write(ring_buffer_t *rb, const void *element);

/**
 * @brief Copy one element out of the buffer.
 *
 * Copies @p element_size bytes from the oldest slot into @p element and
 * advances the tail.
 *
 * @param rb      Ring buffer to read from
 * @param element Destination for the element (at least @p element_size bytes)
 * @return STATUS_OK on success, STATUS_ERR_INVALID_ARG on a NULL pointer,
 *         STATUS_ERR_EMPTY if the buffer is empty.
 */
status_t ring_buffer_read(ring_buffer_t *rb, void *element);

/**
 * @brief Discard all buffered elements.
 *
 * Resets the head and tail to empty. The backing storage is left untouched.
 *
 * @param rb Ring buffer to flush
 * @return STATUS_OK on success, STATUS_ERR_INVALID_ARG if @p rb is NULL.
 */
status_t ring_buffer_flush(ring_buffer_t *rb);

/**
 * @brief Test whether the buffer holds no elements.
 *
 * @param rb Ring buffer to query
 * @return true if empty; false if it holds one or more elements or @p rb is NULL.
 */
bool ring_buffer_is_empty(const ring_buffer_t *rb);

/**
 * @brief Test whether the buffer cannot accept another element.
 *
 * @param rb Ring buffer to query
 * @return true if full; false if space remains or @p rb is NULL.
 */
bool ring_buffer_is_full(const ring_buffer_t *rb);

/** @brief Convenience wrapper: write the lvalue @p val by address. */
#define rb_write(rb, val) ring_buffer_write((rb), &(val))
/** @brief Convenience wrapper: read into the lvalue @p val by address. */
#define rb_read(rb, val) ring_buffer_read((rb), &(val))

#endif /* RING_BUFFER_H */
