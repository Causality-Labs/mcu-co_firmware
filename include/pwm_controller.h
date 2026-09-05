#ifndef PWM_CONTROLLER_H
#define PWM_CONTROLLER_H

#include <stdint.h>
#include "status.h"

/**
 * @brief Handle a PWM_GROUP_CFG (0x40) command payload.
 *
 * @param payload Wire payload, [FREQ_LE32, GROUP]
 * @param length  Payload length in bytes
 * @return STATUS_OK on success, STATUS_ERR_INVALID_ARG on a NULL @p payload.
 */
status_t pwm_controller_group_cfg(const uint8_t *payload, uint8_t length);

/**
 * @brief Handle a PWM_GROUP_RELEASE (0x46) command payload.
 *
 * Tears the group down: counter stopped, all four channels deconfigured, their
 * pins released, timer clock gated off.
 *
 * @param payload Wire payload, [GROUP]
 * @param length  Payload length in bytes
 * @return STATUS_OK on success, STATUS_ERR_INVALID_ARG on a NULL @p payload, a
 *         wrong @p length or a GROUP above 2, STATUS_ERR_NOT_INIT if the group
 *         was never configured.
 */
status_t pwm_controller_group_release(const uint8_t *payload, uint8_t length);

/**
 * @brief Handle a PWM_CFG (0x41) command payload.
 *
 * Claims the pin for PWM at duty 0, so it comes up silent until PWM_SET.
 *
 * @param payload Wire payload, [POL, PORT, PIN]
 * @param length  Payload length in bytes
 * @return STATUS_OK on success, STATUS_ERR_INVALID_ARG on a NULL @p payload, a
 *         wrong @p length or a POL other than 0 or 1, or the driver's status
 *         when the pin has no PWM channel, its group has no frequency, or it is
 *         already claimed.
 */
status_t pwm_controller_channel_cfg(const uint8_t *payload, uint8_t length);

/**
 * @brief Handle a PWM_SET (0x42) command payload.
 *
 * Updates the duty cycle of a pin already claimed by PWM_CFG.
 *
 * @param payload Wire payload, [DUTY_LE16, PORT, PIN]
 * @param length  Payload length in bytes
 * @return STATUS_OK on success, STATUS_ERR_INVALID_ARG on a NULL @p payload.
 */
status_t pwm_controller_channel_set(const uint8_t *payload, uint8_t length);

/**
 * @brief Handle a PWM_RELEASE (0x43) command payload.
 *
 * Releases a pin claimed by PWM_CFG. The group keeps running for the other
 * channels on it; use PWM_GROUP_RELEASE to tear the timer down.
 *
 * @param payload Wire payload, [PORT, PIN]
 * @param length  Payload length in bytes
 * @return STATUS_OK on success, STATUS_ERR_INVALID_ARG on a NULL @p payload.
 */
status_t pwm_controller_channel_release(const uint8_t *payload, uint8_t length);

/**
 * @brief Handle a PWM_GET (0x44) command payload.
 *
 * @param payload       Wire payload, [PORT, PIN]
 * @param length        Payload length in bytes
 * @param duty_permille Output parameter for the duty in tenths of a percent,
 *                      written only on success
 * @return STATUS_OK on success, STATUS_ERR_INVALID_ARG on a NULL @p payload.
 */
status_t pwm_controller_channel_get(const uint8_t *payload, uint8_t length, uint16_t *duty_permille);

/**
 * @brief Handle a PWM_GROUP_GET (0x45) command payload.
 *
 * Reports the frequency the hardware actually produces, which may differ
 * slightly from the one PWM_GROUP_CFG requested.
 *
 * @param payload      Wire payload, [GROUP]
 * @param length       Payload length in bytes
 * @param frequency_hz Output parameter for the achieved frequency in Hz,
 *                     written only on success
 * @return STATUS_OK on success, STATUS_ERR_INVALID_ARG on a NULL @p payload.
 */
status_t pwm_controller_group_get(const uint8_t *payload, uint8_t length, uint32_t *frequency_hz);

#endif /* PWM_CONTROLLER_H */
