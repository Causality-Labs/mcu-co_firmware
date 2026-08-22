#ifndef UART_SPY_H
#define UART_SPY_H

#include "uart.h"

#define UART_SPY_MAX_BYTES 512U

/* Clears every recorded byte and return-value override back to defaults.
 * Call from each test's setup(). */
void UartSpy_Reset(void);

void UartSpy_SetReturnStatus(status_t status); /* applies to every status_t-returning call */

/* Every byte handed to uart_write_byte()/uart_write_buffer(), in order. */
uint16_t UartSpy_GetSentByteCount(void);
uint8_t UartSpy_GetSentByte(uint16_t index);

uart_instance_t UartSpy_GetLastInitInstance(void);
uart_config_t UartSpy_GetLastInitConfig(void);

uint16_t UartSpy_GetDeinitCallCount(void);
uart_instance_t UartSpy_GetLastDeinitInstance(void);

uart_instance_t UartSpy_GetLastReadByteInstance(void);

/* Controls uart_read_byte()'s behavior: if no byte is queued, it returns
 * STATUS_ERR_EMPTY (current behavior). Otherwise it returns the queued byte
 * via `data` and STATUS_OK. */
void UartSpy_SetNextReadByte(uint8_t byte);

#endif /* UART_SPY_H */
