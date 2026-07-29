#include <stdlib.h>
#include "frame_parser.h"

frame_results_t frame_parser_feed(frame_t *frame, uint8_t data_byte)
{
    if (frame == NULL)
    {
        return FRAME_ERROR;
    }

    switch (frame->state)
    {
    case SOF:
        if (data_byte == 0xA5)
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

        if (frame->length > MAX_PAYLOAD)
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

    if (frame->length > MAX_PAYLOAD)
    {
        return -1;
    }

    int size = 2;

    serialized_frame_buffer[OPCODE_IDX] = frame->opcode;
    serialized_frame_buffer[LENGTH_IDX] = frame->length;

    for (uint8_t i = 0; i < frame->length; i++)
    {
        serialized_frame_buffer[PAYLOAD_IDX + i] = frame->payload[i];
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