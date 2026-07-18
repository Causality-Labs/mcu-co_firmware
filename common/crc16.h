#ifndef CRC16_H
#define CRC16_H

#include <stdint.h>
#include <stdbool.h>

#define CRC16_MCUCO 0x1021U

uint16_t crc16_compute(const uint8_t *data_buffer, uint8_t data_size);
bool crc16_compare(uint16_t crc_computed, uint16_t crc_original);

#endif /* CRC16_H */
