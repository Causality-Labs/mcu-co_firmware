#ifndef RING_BUFFER_H
#define RING_BUFFER_H

#include <stdint.h>
#include <stdbool.h>

typedef struct {
    uint8_t  *buffer;
    uint8_t  size;
    uint8_t  head;
    uint8_t  tail;
    uint8_t mask;
} ring_buffer_t;

int ring_buffer_init(ring_buffer_t *ring_buffer, uint8_t *buffer, uint8_t size);
int ring_buffer_write(ring_buffer_t *ring_buffer, uint8_t byte);
int ring_buffer_read(ring_buffer_t *ring_buffer, uint8_t *byte);
int ring_buffer_flush(ring_buffer_t *ring_buffer);

bool is_ring_buffer_empty(const ring_buffer_t *ring_buffer);
bool is_ring_buffer_full(const ring_buffer_t *ring_buffer);


#endif /* RING_BUFFER_H */
