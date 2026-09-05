/*
 * Link-time substitute for src/pwm_controller.c (see pwm_controller.h).
 * Records which function was last called and the raw payload/length it was
 * given, instead of driving any real timer. Linked instead of the real
 * pwm_controller.c when building command_dispatcher's tests, which keeps
 * timer.c and its CMSIS registers out of that binary.
 */
#include <string.h>
#include "pwm_controller_spy.h"

static pwm_controller_call_t last_call;
static uint8_t last_payload[PWM_CONTROLLER_SPY_MAX_PAYLOAD];
static uint8_t last_length;

static uint16_t read_return_duty;
static uint32_t read_return_frequency;
static status_t forced_status;

void PwmControllerSpy_Reset(void)
{
    last_call = PWM_CONTROLLER_CALL_NONE;
    memset(last_payload, 0, sizeof(last_payload));
    last_length = 0;

    read_return_duty      = 0U;
    read_return_frequency = 0U;
    forced_status         = STATUS_OK;
}

void PwmControllerSpy_SetReturnStatus(status_t status)
{
    forced_status = status;
}

void PwmControllerSpy_SetDuty(uint16_t duty_permille)
{
    read_return_duty = duty_permille;
}

void PwmControllerSpy_SetFrequency(uint32_t frequency_hz)
{
    read_return_frequency = frequency_hz;
}

pwm_controller_call_t PwmControllerSpy_GetLastCall(void)
{
    return last_call;
}

const uint8_t *PwmControllerSpy_GetLastPayload(void)
{
    return last_payload;
}

uint8_t PwmControllerSpy_GetLastLength(void)
{
    return last_length;
}

static void record_call(pwm_controller_call_t call, const uint8_t *payload, uint8_t length)
{
    last_call   = call;
    last_length = length;

    uint8_t copy_len = length;
    if (copy_len > sizeof(last_payload))
    {
        copy_len = sizeof(last_payload);
    }

    memset(last_payload, 0, sizeof(last_payload));
    if (payload != NULL)
    {
        memcpy(last_payload, payload, copy_len);
    }
}

/* --- pwm_controller.h implementation --- */

status_t pwm_controller_group_cfg(const uint8_t *payload, uint8_t length)
{
    record_call(PWM_CONTROLLER_CALL_GROUP_CFG, payload, length);
    return forced_status;
}

status_t pwm_controller_group_release(const uint8_t *payload, uint8_t length)
{
    record_call(PWM_CONTROLLER_CALL_GROUP_RELEASE, payload, length);
    return forced_status;
}

status_t pwm_controller_group_get(const uint8_t *payload, uint8_t length, uint32_t *frequency_hz)
{
    record_call(PWM_CONTROLLER_CALL_GROUP_GET, payload, length);

    if (frequency_hz != NULL)
    {
        *frequency_hz = read_return_frequency;
    }

    return forced_status;
}

status_t pwm_controller_channel_cfg(const uint8_t *payload, uint8_t length)
{
    record_call(PWM_CONTROLLER_CALL_CHANNEL_CFG, payload, length);
    return forced_status;
}

status_t pwm_controller_channel_set(const uint8_t *payload, uint8_t length)
{
    record_call(PWM_CONTROLLER_CALL_CHANNEL_SET, payload, length);
    return forced_status;
}

status_t pwm_controller_channel_release(const uint8_t *payload, uint8_t length)
{
    record_call(PWM_CONTROLLER_CALL_CHANNEL_RELEASE, payload, length);
    return forced_status;
}

status_t pwm_controller_channel_get(const uint8_t *payload, uint8_t length, uint16_t *duty_permille)
{
    record_call(PWM_CONTROLLER_CALL_CHANNEL_GET, payload, length);

    if (duty_permille != NULL)
    {
        *duty_permille = read_return_duty;
    }

    return forced_status;
}
