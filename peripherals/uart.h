#ifndef UART_H
#define UART_H

#include <stdint.h>
#include <stdbool.h>
#include "stm32g474xx.h"
#include "gpio.h"
#include "status.h"

#define NUM_OF_UART_PORTS 6

/** @brief Supported baud rates. */
typedef enum
{
    UART_BAUD_9600   = 9600U,
    UART_BAUD_19200  = 19200U,
    UART_BAUD_38400  = 38400U,
    UART_BAUD_57600  = 57600U,
    UART_BAUD_115200 = 115200U,
} uart_baudrate_t;

/** @brief Parity mode selection. */
typedef enum
{
    UART_PARITY_NONE = 0U,
    UART_PARITY_EVEN = 1U,
    UART_PARITY_ODD  = 2U,
} uart_parity_t;

/** @brief Data frame width selection. */
typedef enum
{
    UART_DATA_8BIT = 0U,
    UART_DATA_9BIT = 1U,
    UART_DATA_7BIT = 2U,
} uart_data_width_t;

/** @brief Number of stop bits. */
typedef enum
{
    UART_STOP_1BIT = 0U,
    UART_STOP_2BIT = 1U,
} uart_stop_bits_t;

/** @brief TX, RX, or bidirectional mode. */
typedef enum
{
    UART_MODE_TX    = 0U,
    UART_MODE_RX    = 1U,
    UART_MODE_TX_RX = 2U,
} uart_mode_t;

/** @brief UART peripheral instance identifiers. */
typedef enum
{
    UART_INSTANCE_USART1  = 0U,
    UART_INSTANCE_USART2  = 1U,
    UART_INSTANCE_USART3  = 2U,
    UART_INSTANCE_UART4   = 3U,
    UART_INSTANCE_UART5   = 4U,
    UART_INSTANCE_LPUART1 = 5U,
} uart_instance_t;

/** @brief UART peripheral configuration parameters. */
typedef struct
{
    uart_baudrate_t baudrate;
    uart_parity_t parity;
    uart_data_width_t data_width;
    uart_stop_bits_t stop_bits;
    uart_mode_t mode;
} uart_config_t;

/**
 * @brief Caller-supplied RX ring buffer storage.
 *
 * @p buffer must remain valid for the lifetime of the UART instance.
 * @p size must be a power of 2.
 */
typedef struct
{
    uint8_t *buffer;
    uint16_t size;
} uart_rx_buffer_t;

/**
 * @brief Initialise a UART peripheral.
 *
 * Enables the peripheral clock, configures GPIO pins, baud rate, frame
 * format, and mode. For RX-capable modes, initialises the ring buffer
 * and enables the RXNE interrupt.
 *
 * @param instance  UART peripheral to initialise
 * @param config    Pointer to frame and mode configuration
 * @param rx_buffer Caller-supplied RX buffer; required for RX/TX_RX modes, NULL for TX-only
 * @return STATUS_OK on success, STATUS_ERR_INVALID_ARG on bad arguments,
 *         STATUS_ERR_BUSY if already initialised or its pins are in use,
 *         STATUS_ERR_NOT_INIT if the clock/GPIO could not be brought up,
 *         STATUS_ERR_TIMEOUT if the peripheral did not become ready.
 */
status_t uart_init(uart_instance_t instance, const uart_config_t *config, const uart_rx_buffer_t *rx_buffer);

/**
 * @brief Deinitialise a UART peripheral.
 *
 * Waits for any in-progress transmission to complete, disables the
 * peripheral and its clock, deconfigures GPIO pins, and disables the
 * NVIC interrupt if RX was active.
 *
 * @param instance UART peripheral to deinitialise
 * @return STATUS_OK on success, STATUS_ERR_INVALID_ARG on an invalid instance,
 *         STATUS_ERR_NOT_INIT if the peripheral was not initialised.
 */
status_t uart_deinit(uart_instance_t instance);

/**
 * @brief Transmit a single byte.
 *
 * Blocks until the transmit data register is empty, then writes the byte.
 *
 * @param instance UART peripheral to write to
 * @param data     Byte to transmit
 * @return STATUS_OK on success, STATUS_ERR_INVALID_ARG on an invalid instance,
 *         STATUS_ERR_NOT_INIT if not initialised, STATUS_ERR_INVALID_STATE if
 *         the instance is not in a TX-capable mode, STATUS_ERR_TIMEOUT if the
 *         transmit register did not empty in time.
 */
status_t uart_write_byte(uart_instance_t instance, const uint8_t data);

/**
 * @brief Transmit a buffer of bytes.
 *
 * Calls the internal TX helper for each byte. Fails early if any byte
 * times out waiting for the transmit register to empty.
 *
 * @param instance UART peripheral to write to
 * @param data     Pointer to transmit buffer
 * @param length   Number of bytes to transmit
 * @return STATUS_OK on success, STATUS_ERR_INVALID_ARG on bad arguments,
 *         STATUS_ERR_NOT_INIT if not initialised, STATUS_ERR_INVALID_STATE if
 *         the instance is not in a TX-capable mode, STATUS_ERR_TIMEOUT if a
 *         byte timed out waiting for the transmit register to empty.
 */
status_t uart_write_buffer(uart_instance_t instance, const uint8_t *data, uint16_t length);

/**
 * @brief Read a single byte from the RX ring buffer.
 *
 * Returns immediately if no byte is available.
 *
 * @param instance UART peripheral to read from
 * @param data     Output parameter for the received byte
 * @return STATUS_OK on success, STATUS_ERR_EMPTY if no byte is available,
 *         STATUS_ERR_INVALID_ARG on bad arguments, STATUS_ERR_NOT_INIT if not
 *         initialised, STATUS_ERR_INVALID_STATE if not in an RX-capable mode.
 */
status_t uart_read_byte(uart_instance_t instance, uint8_t *data);

/**
 * @brief Read available bytes from the RX ring buffer.
 *
 * Drains the ring buffer up to @p length bytes. Stops early if the buffer
 * empties before @p length is reached; draining an empty buffer is not an
 * error and yields @p bytes_read == 0 with STATUS_OK.
 *
 * @param instance   UART peripheral to read from
 * @param data       Output buffer to write received bytes into
 * @param length     Maximum number of bytes to read
 * @param bytes_read Output parameter set to the number of bytes read (0 to length)
 * @return STATUS_OK on success, STATUS_ERR_INVALID_ARG on bad arguments,
 *         STATUS_ERR_NOT_INIT if not initialised, STATUS_ERR_INVALID_STATE if
 *         the instance is not in an RX-capable mode.
 */
status_t uart_read_buffer(uart_instance_t instance, uint8_t *data, uint16_t length, uint16_t *bytes_read);

#endif /* UART_H */
