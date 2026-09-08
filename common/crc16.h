#ifndef CRC16_H
#define CRC16_H

#include <stdint.h>
#include <stdbool.h>

/** @brief CRC16-CCITT generator polynomial (x^16 + x^12 + x^5 + 1). */
#define CRC16_MCUCO 0x1021U

/**
 * @brief Compute the CRC16-CCITT of a byte range.
 *
 * MSB-first over ::CRC16_MCUCO, seeded with 0xFFFF, unreflected, no final XOR.
 * A NULL @p data_buffer returns the bare seed rather than an error, since every
 * value is a legal checksum - check the pointer yourself if that matters.
 *
 * @param data_buffer Bytes to checksum
 * @param data_size   Number of bytes, at most 255
 * @return CRC16 over @p data_buffer, or the 0xFFFF seed if it is NULL.
 */
uint16_t crc16_compute(const uint8_t *data_buffer, uint8_t data_size);

/**
 * @brief Compare a computed checksum against a received one.
 *
 * @param crc_computed Checksum computed over the received bytes
 * @param crc_original Checksum the frame carried
 * @return true if they match.
 */
bool crc16_compare(uint16_t crc_computed, uint16_t crc_original);

#endif /* CRC16_H */
