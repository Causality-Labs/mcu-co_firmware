#ifndef GPIO_CONTROLLER_H
#define GPIO_CONTROLLER_H

#include <stdbool.h>
#include <stdint.h>
#include "status.h"

/*
 * Command-layer handlers for the GPIO opcodes; the wire format is specified in
 * mcu-co_Protocol.md. Each validates the payload before the driver sees it.
 *
 * Shared: a NULL payload or a wrong length is STATUS_ERR_INVALID_ARG; a PORT
 * above 6 (GPIOA-G), a PIN above 15 and gpio.c's reserved pins are
 * STATUS_ERR_INVALID_PIN.
 */

/**
 * @brief GPIO_CFG (0x30): configure a pin as a push-pull input or output.
 *
 * @param payload [DIR, PORT, PIN]; DIR 0 = input, 1 = output
 * @param length  Payload length in bytes
 * @return STATUS_OK, STATUS_ERR_INVALID_ARG on a bad DIR, or the driver's status.
 */
status_t gpio_controller_io_cfg(const uint8_t *payload, uint8_t length);

/**
 * @brief GPIO_WRITE (0x31): drive a configured output pin.
 *
 * @param payload [LEVEL, PORT, PIN]; LEVEL 0 = low, 1 = high
 * @param length  Payload length in bytes
 * @return STATUS_OK, STATUS_ERR_INVALID_ARG on a bad LEVEL,
 *         STATUS_ERR_INVALID_STATE if the pin is not an output.
 */
status_t gpio_controller_write(const uint8_t *payload, uint8_t length);

/**
 * @brief GPIO_READ (0x32): sample a configured input pin.
 *
 * @param payload [PORT, PIN]
 * @param length  Payload length in bytes
 * @param state   Output parameter for the pin level, written only on success
 * @return STATUS_OK, STATUS_ERR_INVALID_ARG if @p state is NULL,
 *         STATUS_ERR_INVALID_STATE if the pin is not an input.
 */
status_t gpio_controller_read(const uint8_t *payload, uint8_t length, bool *state);

/**
 * @brief GPIO_IRQ_CFG (0x34): arm or disarm a pin's EXTI trigger.
 *
 * Leaves the pin unbound either way; gpio_controller_irq_bind() attaches the
 * action.
 *
 * @param payload [EDGE, PORT, PIN]; EDGE 0 = disarm, 1 = rising, 2 = falling,
 *                3 = both
 * @param length  Payload length in bytes
 * @return STATUS_OK, STATUS_ERR_INVALID_ARG on a bad EDGE, STATUS_ERR if the
 *         driver refused - usually another port already owns this EXTI line.
 */
status_t gpio_controller_irq_cfg(const uint8_t *payload, uint8_t length);

/**
 * @brief GPIO_IRQ_BIND (0x33): drive an output pin from an armed input's ISR.
 *
 * Both pins must already be configured, and EDGE must be exactly the edge
 * GPIO_IRQ_CFG armed. An existing binding is never silently replaced.
 *
 * @param payload [EDGE, IN_PORT, IN_PIN, ACTION, OUT_PORT, OUT_PIN]; ACTION
 *                0 = drive low, 1 = drive high, 2 = toggle
 * @param length  Payload length in bytes
 * @return STATUS_OK, STATUS_ERR_INVALID_ARG on a bad EDGE or ACTION,
 *         STATUS_ERR_INVALID_STATE on a pin-direction mismatch or an unarmed or
 *         differently armed edge, STATUS_ERR_BUSY if the input pin is bound.
 */
status_t gpio_controller_irq_bind(const uint8_t *payload, uint8_t length);

/**
 * @brief GPIO_IRQ_UNBIND (0x35): drop a pin's binding, leaving it armed.
 *
 * @param payload [PORT, PIN]
 * @param length  Payload length in bytes
 * @return STATUS_OK, STATUS_ERR_INVALID_STATE if the pin has no binding.
 */
status_t gpio_controller_irq_unbind(const uint8_t *payload, uint8_t length);

#endif /* GPIO_CONTROLLER_H */
