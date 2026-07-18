#include <stddef.h>
#include "crc16.h"

#define CRC16_CCITT_INIT 0xFFFFU

uint16_t crc16_compute(const uint8_t *data_buffer, uint8_t data_size)
{
    uint16_t crc = CRC16_CCITT_INIT;

    if (data_buffer == NULL)
    {
        return crc;
    }

    for (uint8_t i = 0; i < data_size; i++)
    {
        crc = (uint16_t)(crc ^ (uint16_t)((uint16_t)data_buffer[i] << 8));

        for (uint8_t bit = 0; bit < 8U; bit++)
        {
            if ((crc & 0x8000U) != 0U)
            {
                crc = (uint16_t)((uint16_t)(crc << 1) ^ CRC16_MCUCO);
            }
            else
            {
                crc = (uint16_t)(crc << 1);
            }
        }
    }

    return crc;
}

bool crc16_compare(uint16_t crc_computed, uint16_t crc_original)
{
    return (crc_computed == crc_original);
}