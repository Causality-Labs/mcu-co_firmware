#ifndef FRAME_PARSER_H
#define FRAME_PARSER_H

#include <stdint.h>
#include <stdbool.h>

/** @brief Longest PAYLOAD a command frame may declare; a longer LENGTH is rejected. */
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

/** @brief What one byte did to the frame being assembled. */
typedef enum
{
    FRAME_READY,
    FRAME_IN_PROGRESS,
    FRAME_PENDING,
    FRAME_ERROR
} frame_results_t;

/** @brief Which field of the wire format the next byte belongs to. */
typedef enum
{
    SOF,
    OPCODE,
    LENGTH,
    PAYLOAD,
    CRC_LOW,
    CRC_HIGH
} frame_state_t;

/**
 * @brief A command frame under construction, and the parser's state with it.
 *
 * One instance is fed bytes for the life of the link, not one per frame. Start
 * it zeroed with @p state set to ::SOF.
 */
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

/**
 * @brief Advance the frame state machine by one received byte.
 *
 * ::FRAME_READY means complete, not trusted - the CRC still has to be checked
 * with frame_parser_serialize() and frame_parser_get_crc(). The next call starts
 * the following frame, so consume the completed one first.
 *
 * @param frame     Frame being assembled
 * @param data_byte Byte just received
 * @return ::FRAME_PENDING while hunting for the start-of-frame marker,
 *         ::FRAME_IN_PROGRESS once a byte has been taken into the frame,
 *         ::FRAME_READY when the last CRC byte completes it, ::FRAME_ERROR on a
 *         NULL @p frame or a LENGTH above ::RX_MAX_PAYLOAD.
 */
frame_results_t frame_parser_feed(command_frame_t *frame, uint8_t data_byte);

/**
 * @brief Lay a received frame's CRC-covered bytes out contiguously.
 *
 * Writes OPCODE, LENGTH and PAYLOAD - exactly the range crc16_compute() runs
 * over, with the start-of-frame marker and the CRC excluded.
 *
 * @param frame                   Frame to serialise
 * @param serialized_frame_buffer Destination buffer
 * @param serialized_frame_size   Capacity of @p serialized_frame_buffer
 * @return Number of bytes written (2 + the frame's LENGTH), or -1 on a NULL
 *         pointer, a buffer too small to hold them, or a LENGTH above
 *         ::RX_MAX_PAYLOAD.
 */
int frame_parser_serialize(command_frame_t *frame, uint8_t *serialized_frame_buffer, uint8_t serialized_frame_size);

/**
 * @brief Build a response frame's wire bytes, up to but excluding the CRC.
 *
 * LEN counts the ACK byte along with DATA. Finish the frame with
 * frame_parser_append_crc() over the bytes from ::TX_LEN_IDX onward.
 *
 * @param response                Reply to serialise
 * @param serialized_response     Destination buffer, ::TX_FRAME_MAX bytes is
 *                                always enough
 * @param serialized_response_size Capacity of @p serialized_response
 * @return Number of bytes written (3 + @p response's data_len), or -1 on a NULL
 *         pointer, a data_len above ::TX_DATA_MAX, or a buffer too small.
 */
int frame_parser_serialize_response(response_frame_t *response, uint8_t *serialized_response,
                                    uint8_t serialized_response_size);

/**
 * @brief Append a little-endian CRC to a partly built frame.
 *
 * @param serialized_frame_buffer Frame buffer to append to
 * @param current_length          Bytes already in the buffer; the CRC lands here
 * @param buffer_size             Capacity of @p serialized_frame_buffer
 * @param crc                     Checksum to append
 * @return The frame's total length (@p current_length + 2), or -1 on a NULL
 *         pointer or a buffer with no room for both CRC bytes.
 */
int frame_parser_append_crc(uint8_t *serialized_frame_buffer, uint8_t current_length, uint8_t buffer_size, uint16_t crc);

/**
 * @brief Recombine a received frame's two CRC bytes into one value.
 *
 * @param frame Frame whose CRC bytes have been received
 * @param crc   Output parameter for the checksum the frame carried
 * @return 0 on success, -1 if either pointer is NULL.
 */
int frame_parser_get_crc(command_frame_t *frame, uint16_t *crc);

#endif /* FRAME_PARSER_H */
