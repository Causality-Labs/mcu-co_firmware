#ifndef FRAME_PARSER_H
#define FRAME_PARSER_H

#include <stdint.h>
#include <stdbool.h>

#define RX_MAX_PAYLOAD 32
#define RX_OPCODE_IDX  0
#define RX_LENGTH_IDX  1
#define RX_PAYLOAD_IDX 2

#define TX_SOF_IDX  0
#define TX_LEN_IDX  1
#define TX_ACK_IDX  2
#define TX_DATA_IDX 3

/* Widest response payload: PWM_GROUP_GET's 32-bit achieved frequency. */
#define TX_DATA_MAX 4

#define TX_FRAME_MAX 9 /* SOF + LEN + ACK + TX_DATA_MAX + CRC_L + CRC_H */

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
    uint8_t payload[RX_MAX_PAYLOAD];
    uint8_t payload_idx;
    uint8_t crc_low;
    uint8_t crc_high;
} command_frame_t;

/**
 * @brief A command's reply: an outcome, plus the value a read command returns.
 *
 * @p data_len is fixed per opcode rather than per outcome — a read NACKs with
 * its data bytes zeroed rather than absent, so the host can size the frame
 * before it knows whether the command succeeded.
 */
typedef struct
{
    bool ack;
    uint8_t data[TX_DATA_MAX];
    uint8_t data_len;
} response_frame_t;

frame_results_t frame_parser_feed(command_frame_t *frame, uint8_t data_byte);

int frame_parser_serialize(command_frame_t *frame, uint8_t *serialized_frame_buffer, uint8_t serialized_frame_size);

int frame_parser_serialize_response(response_frame_t *response, uint8_t *serialized_response,
                                    uint8_t serialized_response_size);

int frame_parser_append_crc(uint8_t *serialized_frame_buffer, uint8_t current_length, uint8_t buffer_size, uint16_t crc);

int frame_parser_get_crc(command_frame_t *frame, uint16_t *crc);

#endif /* FRAME_PARSER_H */