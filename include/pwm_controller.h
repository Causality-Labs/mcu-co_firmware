#ifndef PWM_CONTROLLER_H
#define PWM_CONTROLLER_H

#include <stdint.h>
#include "status.h"

/*
 * Command-layer handlers for the PWM opcodes; the wire format is specified in
 * mcu-co_Protocol.md. Each validates the payload before the driver sees it.
 *
 * Shared: a NULL payload, a wrong length, a NULL output parameter or a GROUP
 * above 2 (0 = TIM2, 1 = TIM3, 2 = TIM4) is STATUS_ERR_INVALID_ARG. A PORT above
 * 6 (GPIOA-G) or a PIN above 15 is STATUS_ERR_INVALID_PIN; an in-range pin with
 * no PWM channel is STATUS_ERR_UNSUPPORTED. A group is one timer, so its four
 * channels share a frequency; DUTY is per channel, in tenths of a percent.
 */

/**
 * @brief PWM_GROUP_CFG (0x40): set a group's frequency and start its counter.
 *
 * Channels stay silent until PWM_CFG claims them. STATUS_ERR_BUSY is the only
 * failure that leaves the group running; every other one leaves it off.
 *
 * @param payload [FREQ_LE32, GROUP]
 * @param length  Payload length in bytes
 * @return STATUS_OK, STATUS_ERR_INVALID_ARG on a FREQ outside 1 to 1000000 Hz or
 *         unreachable from the timer clock, STATUS_ERR_BUSY if the group is
 *         already configured, STATUS_ERR_NOT_INIT if rcc_init() has not run.
 */
status_t pwm_controller_group_cfg(const uint8_t *payload, uint8_t length);

/**
 * @brief PWM_GROUP_RELEASE (0x46): tear a group down.
 *
 * Counter stopped, all four channels deconfigured, pins released, clock gated
 * off. The only way to change a configured group's frequency.
 *
 * @param payload [GROUP]
 * @param length  Payload length in bytes
 * @return STATUS_OK, STATUS_ERR_NOT_INIT if the group was never configured.
 */
status_t pwm_controller_group_release(const uint8_t *payload, uint8_t length);

/**
 * @brief PWM_CFG (0x41): claim a pin for PWM at duty 0.
 *
 * The pin comes up silent until PWM_SET. Its group must already have a
 * frequency; this does not bring one up.
 *
 * @param payload [POL, PORT, PIN]; POL 0 = active-high, 1 = active-low
 * @param length  Payload length in bytes
 * @return STATUS_OK, STATUS_ERR_INVALID_ARG on a bad POL, STATUS_ERR_NOT_INIT if
 *         the group has no frequency, STATUS_ERR_BUSY if the channel is claimed
 *         or another driver owns the pin.
 */
status_t pwm_controller_channel_cfg(const uint8_t *payload, uint8_t length);

/**
 * @brief PWM_SET (0x42): update a claimed pin's duty cycle.
 *
 * Buffered by the hardware and applied at the next period boundary, so it never
 * produces a partial pulse. A duty of 0 is how an output is silenced.
 *
 * @param payload [DUTY_LE16, PORT, PIN]
 * @param length  Payload length in bytes
 * @return STATUS_OK, STATUS_ERR_INVALID_ARG on a DUTY above 1000,
 *         STATUS_ERR_NOT_INIT if the pin was never claimed.
 */
status_t pwm_controller_channel_set(const uint8_t *payload, uint8_t length);

/**
 * @brief PWM_RELEASE (0x43): release a claimed pin, leaving its group running.
 *
 * @param payload [PORT, PIN]
 * @param length  Payload length in bytes
 * @return STATUS_OK, STATUS_ERR_NOT_INIT if the pin was never claimed.
 */
status_t pwm_controller_channel_release(const uint8_t *payload, uint8_t length);

/**
 * @brief PWM_GET (0x44): read back a claimed pin's duty cycle.
 *
 * Reports the duty last requested, not one re-derived from the compare register,
 * which truncates.
 *
 * @param payload       [PORT, PIN]
 * @param length        Payload length in bytes
 * @param duty_permille Output parameter for the duty in tenths of a percent,
 *                      written only on success
 * @return STATUS_OK, STATUS_ERR_NOT_INIT if the pin was never claimed.
 */
status_t pwm_controller_channel_get(const uint8_t *payload, uint8_t length, uint16_t *duty_permille);

/**
 * @brief PWM_GROUP_GET (0x45): read back a group's achieved frequency.
 *
 * The prescaler and reload are integers, so this can differ slightly from what
 * PWM_GROUP_CFG requested.
 *
 * @param payload      [GROUP]
 * @param length       Payload length in bytes
 * @param frequency_hz Output parameter for the achieved frequency in Hz, written
 *                     only on success
 * @return STATUS_OK, STATUS_ERR_NOT_INIT if the group has no frequency yet or
 *         rcc_init() has not run.
 */
status_t pwm_controller_group_get(const uint8_t *payload, uint8_t length, uint32_t *frequency_hz);

#endif /* PWM_CONTROLLER_H */
