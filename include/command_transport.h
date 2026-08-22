#ifndef COMMAND_TRANSPORT_H
#define COMMAND_TRANSPORT_H

#include "status.h"
#include "uart.h"

/*
 * Single-instance module: the transport owns one UART link to the host, and
 * its state (chosen instance, RX buffer) is file-scope in command_transport.c.
 * The instance is selectable at init, but only one may be active at a time -
 * call command_transport_deinit() before initialising on a different one.
 */

/**
 * @brief Initialise the command transport on the given UART instance.
 *
 * Brings up @p instance for TX/RX command traffic.
 *
 * @param instance UART peripheral to use for command traffic
 * @return STATUS_OK on success, STATUS_ERR_BUSY if already initialised, or
 *         the error returned by uart_init().
 */
status_t command_transport_init(uart_instance_t instance);

/**
 * @brief Deinitialise the command transport.
 *
 * Calls uart_deinit() and clears the initialised state. Safe to call even
 * if command_transport_init() was never called - a no-op returning
 * STATUS_OK, not an error.
 *
 * @return STATUS_OK on success (including when already deinitialised), or
 *         the error returned by uart_deinit().
 */
status_t command_transport_deinit(void);

/**
 * @brief Receive a single byte from the host, if one is available.
 *
 * Thin wrapper over uart_read_byte() using the instance chosen at init.
 *
 * @param data Output parameter for the received byte
 * @return STATUS_OK on success, STATUS_ERR_EMPTY if no byte is available,
 *         STATUS_ERR_INVALID_ARG if @p data is NULL,
 *         STATUS_ERR_NOT_INIT if command_transport_init() has not been
 *         called (or was deinitialised).
 */
status_t command_transport_receive(uint8_t *data);

/**
 * @brief Send a pre-built frame to the host.
 *
 * Thin wrapper over uart_write_buffer() using the instance chosen at init.
 * Framing/encoding is not this module's job - the caller builds the wire
 * bytes (see frame_parser) and hands them here to go out over UART.
 *
 * @param frame Bytes to send
 * @param length Number of bytes in @p frame
 * @return STATUS_OK on success, STATUS_ERR_INVALID_ARG if @p frame is NULL,
 *         STATUS_ERR_NOT_INIT if command_transport_init() has not been
 *         called (or was deinitialised), or the error returned by the
 *         UART write.
 */
status_t command_transport_send(const uint8_t *frame, uint16_t length);

#endif /* COMMAND_TRANSPORT_H */
