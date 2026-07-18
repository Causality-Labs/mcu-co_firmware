#ifndef FRAME_PARSER_H
#define FRAME_PARSER_H

#include <stdint.h>

#define MAX_PAYLOAD 32
#define OPCODE_IDX  0
#define LENGTH_IDX  1
#define PAYLOAD_IDX 2

typedef enum
{
    FRAME_READY,
    FRAME_IN_PROGRESS,
    FRAME_PENDING,
    FRAME_ERROR
} frame_results_t;

typedef enum
{
    SOF,
    OPCODE,
    LENGTH,
    PAYLOAD,
    CRC_LOW,
    CRC_HIGH
} frame_state_t;

typedef struct
{
    frame_state_t state;
    uint8_t opcode;
    uint8_t length;
    uint8_t payload[MAX_PAYLOAD];
    uint8_t payload_idx;
    uint8_t crc_low;
    uint8_t crc_high;
} frame_t;

frame_results_t frame_parser_feed(frame_t *frame, uint8_t data_byte);

int frame_parser_serialize(frame_t *frame, uint8_t *serialized_frame_buffer, uint8_t serialized_frame_size);

int frame_parser_get_crc(frame_t *frame, uint16_t *crc);

#endif /* FRAME_PARSER_H */