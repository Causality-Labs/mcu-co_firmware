#include <stdlib.h>
#include "frame_parser.h"

#define TX_HEADER_LEN      2U
#define TX_BODY_NO_STATE   1U
#define TX_BODY_WITH_STATE 2U

#define SOF_BYTE 0xA5

#define CRC_SIZE 2U

frame_results_t frame_parser_feed(frame_t *frame, uint8_t data_byte)
{
    if (frame == NULL)
    {
        return FRAME_ERROR;
    }

    switch (frame->state)
    {
    case SOF:
        if (data_byte == SOF_BYTE)
        {
            frame->state = OPCODE;
            return FRAME_IN_PROGRESS;
        }
        else
        {
            return FRAME_PENDING;
        }

    case OPCODE:
        frame->opcode = data_byte;
        frame->state  = LENGTH;

        return FRAME_IN_PROGRESS;

    case LENGTH:
        frame->length = data_byte;

        if (frame->length > RX_MAX_PAYLOAD)
        {
            frame->state = SOF;

            return FRAME_ERROR;
        }
        else if (frame->length == 0U)
        {
            frame->state = CRC_LOW;

            return FRAME_IN_PROGRESS;
        }
        else
        {
            frame->payload_idx = 0;
            frame->state       = PAYLOAD;

            return FRAME_IN_PROGRESS;
        }

    case PAYLOAD:
        frame->payload[frame->payload_idx] = data_byte;
        frame->payload_idx++;

        if (frame->payload_idx == frame->length)
        {
            frame->payload_idx = 0;
            frame->state       = CRC_LOW;
        }

        return FRAME_IN_PROGRESS;

    case CRC_LOW:
        frame->crc_low = data_byte;
        frame->state   = CRC_HIGH;

        return FRAME_IN_PROGRESS;

    case CRC_HIGH:
        frame->crc_high = data_byte;
        frame->state    = SOF;

        return FRAME_READY;

    default:
        frame->state = SOF;

        return FRAME_READY;
    }

    return FRAME_READY;
}

int frame_parser_serialize(frame_t *frame, uint8_t *serialized_frame_buffer, uint8_t serialized_frame_size)
{
    if (frame == NULL || serialized_frame_buffer == NULL)
    {
        return -1;
    }

    if (serialized_frame_size < (2 + frame->length))
    {
        return -1;
    }

    if (frame->length > RX_MAX_PAYLOAD)
    {
        return -1;
    }

    int size = 2;

    serialized_frame_buffer[RX_OPCODE_IDX] = frame->opcode;
    serialized_frame_buffer[RX_LENGTH_IDX] = frame->length;

    for (uint8_t i = 0; i < frame->length; i++)
    {
        serialized_frame_buffer[RX_PAYLOAD_IDX + i] = frame->payload[i];
        size++;
    }

    return size;
}

int frame_parser_get_crc(frame_t *frame, uint16_t *crc)
{
    if (frame == NULL || crc == NULL)
    {
        return -1;
    }

    *crc = (uint16_t)((uint16_t)frame->crc_high << 8 | frame->crc_low);

    return 0;
}

int frame_parser_serialize_response(response_t *response, uint8_t *serialized_response, uint8_t serialized_response_size)
{
    if (response == NULL || serialized_response == NULL)
    {
        return -1;
    }

    uint8_t body_len  = response->has_state ? TX_BODY_WITH_STATE : TX_BODY_NO_STATE;
    uint8_t frame_len = (uint8_t)(TX_HEADER_LEN + body_len);

    if (serialized_response_size < frame_len)
    {
        return -1;
    }

    serialized_response[TX_SOF_IDX] = SOF_BYTE;
    serialized_response[TX_LEN_IDX] = body_len;
    serialized_response[TX_ACK_IDX] = response->ack ? 1U : 0U;

    if (response->has_state == true)
    {
        serialized_response[TX_STATE_IDX] = response->state;
    }

    return (int)frame_len;
}

//
int frame_parser_append_crc(uint8_t *serialized_frame_buffer, uint8_t current_length, uint8_t buffer_size, uint16_t crc)
{
    if (serialized_frame_buffer == NULL)
    {
        return -1;
    }

    /* Widened deliberately: current_length + CRC_SIZE wraps to 0 in uint8_t
     * arithmetic at current_length = 254, which would let the check pass and
     * the writes below land past the end of the buffer. */
    if (((uint16_t)current_length + CRC_SIZE) > (uint16_t)buffer_size)
    {
        return -1;
    }

    serialized_frame_buffer[current_length]      = (uint8_t)(crc & 0x00FFU);
    serialized_frame_buffer[current_length + 1U] = (uint8_t)(crc >> 8);

    int frame_size = current_length + CRC_SIZE;

    return frame_size;
}
