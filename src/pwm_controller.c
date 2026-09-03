#include <stddef.h>
#include <stdint.h>
#include "logger.h"
#include "pwm_controller.h"
#include "status.h"
#include "timer.h"

#define MODULE_NAME "PWM_CONTROLLER"

#define PWM_GROUP_CFG_PAYLOAD_LEN 5U
#define PWM_GROUP_CFG_FREQ_IDX    0U
#define PWM_GROUP_CFG_GROUP_IDX   4U

#define PWM_GROUP_RELEASE_PAYLOAD_LEN 1U
#define PWM_GROUP_RELEASE_GROUP_IDX 0U

#define PWM_CHANNEL_CFG_PAYLOAD_LEN 3U
#define PWM_CHANNEL_CFG_POL_IDX     0U
#define PWM_CHANNEL_CFG_PORT_IDX    1U
#define PWM_CHANNEL_CFG_PIN_IDX     2U

#define PWM_CHANNEL_SET_PAYLOAD_LEN 4U
#define PWM_CHANNEL_SET_DUTY_IDX    0U
#define PWM_CHANNEL_SET_PORT_IDX    2U
#define PWM_CHANNEL_SET_PIN_IDX     3U

#define PWM_CHANNEL_RELEASE_PAYLOAD_LEN 2U
#define PWM_CHANNEL_RELEASE_PORT_IDX    0U
#define PWM_CHANNEL_RELEASE_PIN_IDX     1U

#define PWM_CHANNEL_GET_PAYLOAD_LEN 2U
#define PWM_CHANNEL_GET_PORT_IDX    0U
#define PWM_CHANNEL_GET_PIN_IDX     1U

#define PWM_GROUP_GET_PAYLOAD_LEN 1U
#define PWM_GROUP_GET_GROUP_IDX   0U

static uint32_t read_uint32_le(const uint8_t *bytes)
{
    uint32_t number = 0;

    number = (uint32_t)bytes[0];
    number |= (uint32_t)bytes[1] << 8;
    number |= (uint32_t)bytes[2] << 16;
    number |= (uint32_t)bytes[3] << 24;

    return number;
}

static uint16_t read_uint16_le(const uint8_t *bytes)
{
    uint16_t number = 0;

    number = (uint16_t)bytes[0];
    number |= (uint16_t)((uint16_t)bytes[1] << 8);

    return number;
}

/* Four of the five channel commands carry the same PORT/PIN pair and need the
 * same two things done with it: bound both fields before the cast to
 * gpio_port_t, then ask the driver which timer and channel own the pin. Kept in
 * one place so the bounds and the "look the pin up, never encode the map" rule
 * cannot drift apart between commands. */
static status_t resolve_channel(uint8_t port, uint8_t pin, timer_instance_t *instance, timer_channel_t *channel)
{
    if (port >= GPIO_NUM_OF_PORTS)
    {
        LOG_ERROR(MODULE_NAME, "invalid port %u", port);
        return STATUS_ERR_INVALID_PIN;
    }

    if (pin > MAX_PIN_COUNT)
    {
        LOG_ERROR(MODULE_NAME, "invalid pin %u", pin);
        return STATUS_ERR_INVALID_PIN;
    }

    const gpio_pin_t gpio_pin = {
        .port = (gpio_port_t)port,
        .pin  = pin,
    };

    status_t status_ret = timer_pwm_lookup_pin(&gpio_pin, instance, channel);

    if (status_ret != STATUS_OK)
    {
        LOG_ERROR(MODULE_NAME, "no PWM channel on port %u pin %u", port, pin);
    }

    return status_ret;
}

status_t pwm_controller_group_cfg(const uint8_t *payload, uint8_t length)
{

    if (payload == NULL)
    {
        LOG_ERROR(MODULE_NAME, "payload argument is null");
        return STATUS_ERR_INVALID_ARG;
    }

    if (length != PWM_GROUP_CFG_PAYLOAD_LEN)
    {
        LOG_ERROR(MODULE_NAME, "invalid length %u, expected %u", length, PWM_GROUP_CFG_PAYLOAD_LEN);
        return STATUS_ERR_INVALID_ARG;
    }

    uint8_t group = payload[PWM_GROUP_CFG_GROUP_IDX];

    if (group >= TIMER_INSTANCE_COUNT)
    {
        LOG_ERROR(MODULE_NAME, "invalid group %u", group);
        return STATUS_ERR_INVALID_ARG;
    }

    timer_instance_t instance = (timer_instance_t)group;

    uint32_t frequency = read_uint32_le(&payload[PWM_GROUP_CFG_FREQ_IDX]);

    status_t status_ret = timer_init(instance, frequency);

    if (status_ret != STATUS_OK)
    {
        LOG_ERROR(MODULE_NAME, "timer_init() failed on group %u: %s", group, status_to_str(status_ret));
        return status_ret;
    }

    status_ret = timer_start(instance);

    if (status_ret != STATUS_OK)
    {
        LOG_ERROR(MODULE_NAME, "timer_start() failed on group %u: %s", group, status_to_str(status_ret));
        (void)timer_deinit(instance);
        return status_ret;
    }

    return STATUS_OK;
}

status_t pwm_controller_group_release(const uint8_t *payload, uint8_t length)
{
    if (payload == NULL)
    {
        LOG_ERROR(MODULE_NAME, "payload argument is null");
        return STATUS_ERR_INVALID_ARG;
    }

    if (length != PWM_GROUP_RELEASE_PAYLOAD_LEN)
    {
        LOG_ERROR(MODULE_NAME, "invalid length %u, expected %u", length, PWM_GROUP_RELEASE_PAYLOAD_LEN);
        return STATUS_ERR_INVALID_ARG;
    }

    uint8_t group = payload[PWM_GROUP_RELEASE_GROUP_IDX];

    if (group >= TIMER_INSTANCE_COUNT)
    {
        LOG_ERROR(MODULE_NAME, "invalid group %u", group);
        return STATUS_ERR_INVALID_ARG;
    }

    timer_instance_t instance = (timer_instance_t)group;

    return timer_deinit(instance);
}

status_t pwm_controller_channel_cfg(const uint8_t *payload, uint8_t length)
{
    if (payload == NULL)
    {
        LOG_ERROR(MODULE_NAME, "payload argument is null");
        return STATUS_ERR_INVALID_ARG;
    }

    if (length != PWM_CHANNEL_CFG_PAYLOAD_LEN)
    {
        LOG_ERROR(MODULE_NAME, "invalid length %u, expected %u", length, PWM_CHANNEL_CFG_PAYLOAD_LEN);
        return STATUS_ERR_INVALID_ARG;
    }

    uint8_t polarity = payload[PWM_CHANNEL_CFG_POL_IDX];

    if ((polarity != TIMER_POLARITY_ACTIVE_LOW) &&
        (polarity != TIMER_POLARITY_ACTIVE_HIGH))
    {
        LOG_ERROR(MODULE_NAME, "invalid polarity %u", polarity);
        return STATUS_ERR_INVALID_ARG;
    }

    uint8_t port = payload[PWM_CHANNEL_CFG_PORT_IDX];
    uint8_t pin  = payload[PWM_CHANNEL_CFG_PIN_IDX];

    /* Given values the lookup overwrites: the compiler cannot see through the
     * call, so -Wmaybe-uninitialized fires without them. */
    timer_instance_t instance = TIMER_TIM2;
    timer_channel_t channel   = TIMER_CH1;

    status_t status_ret = resolve_channel(port, pin, &instance, &channel);

    if (status_ret != STATUS_OK)
    {
        return status_ret;
    }

    const timer_pwm_config_t pwm_config = {
        .duty_permille = 0U,
        .polarity      = (timer_polarity_t)polarity,
    };

    return timer_pwm_channel_init(instance, channel, &pwm_config);
}

status_t pwm_controller_channel_set(const uint8_t *payload, uint8_t length)
{
    if (payload == NULL)
    {
        LOG_ERROR(MODULE_NAME, "payload argument is null");
        return STATUS_ERR_INVALID_ARG;
    }

    if (length != PWM_CHANNEL_SET_PAYLOAD_LEN)
    {
        LOG_ERROR(MODULE_NAME, "invalid length %u, expected %u", length, PWM_CHANNEL_SET_PAYLOAD_LEN);
        return STATUS_ERR_INVALID_ARG;
    }

    uint8_t port = payload[PWM_CHANNEL_SET_PORT_IDX];
    uint8_t pin  = payload[PWM_CHANNEL_SET_PIN_IDX];

    /* Given values the lookup overwrites: the compiler cannot see through the
     * call, so -Wmaybe-uninitialized fires without them. */
    timer_instance_t instance = TIMER_TIM2;
    timer_channel_t channel   = TIMER_CH1;

    status_t status_ret = resolve_channel(port, pin, &instance, &channel);

    if (status_ret != STATUS_OK)
    {
        return status_ret;
    }

    uint16_t duty = read_uint16_le(&payload[PWM_CHANNEL_SET_DUTY_IDX]);

    return timer_pwm_set_duty(instance, channel, duty);
}

status_t pwm_controller_channel_release(const uint8_t *payload, uint8_t length)
{
    if (payload == NULL)
    {
        LOG_ERROR(MODULE_NAME, "payload argument is null");
        return STATUS_ERR_INVALID_ARG;
    }

    if (length != PWM_CHANNEL_RELEASE_PAYLOAD_LEN)
    {
        LOG_ERROR(MODULE_NAME, "invalid length %u, expected %u", length, PWM_CHANNEL_RELEASE_PAYLOAD_LEN);
        return STATUS_ERR_INVALID_ARG;
    }

    uint8_t port = payload[PWM_CHANNEL_RELEASE_PORT_IDX];
    uint8_t pin  = payload[PWM_CHANNEL_RELEASE_PIN_IDX];

    /* Given values the lookup overwrites: the compiler cannot see through the
     * call, so -Wmaybe-uninitialized fires without them. */
    timer_instance_t instance = TIMER_TIM2;
    timer_channel_t channel   = TIMER_CH1;

    status_t status_ret = resolve_channel(port, pin, &instance, &channel);

    if (status_ret != STATUS_OK)
    {
        return status_ret;
    }

    return timer_pwm_channel_deinit(instance, channel);
}

status_t pwm_controller_channel_get(const uint8_t *payload, uint8_t length, uint16_t *duty_permille)
{
    if (payload == NULL)
    {
        LOG_ERROR(MODULE_NAME, "payload argument is null");
        return STATUS_ERR_INVALID_ARG;
    }

    if (length != PWM_CHANNEL_GET_PAYLOAD_LEN)
    {
        LOG_ERROR(MODULE_NAME, "invalid length %u, expected %u", length, PWM_CHANNEL_GET_PAYLOAD_LEN);
        return STATUS_ERR_INVALID_ARG;
    }

    if (duty_permille == NULL)
    {
        LOG_ERROR(MODULE_NAME, "duty output argument is null");
        return STATUS_ERR_INVALID_ARG;
    }

    uint8_t port = payload[PWM_CHANNEL_GET_PORT_IDX];
    uint8_t pin  = payload[PWM_CHANNEL_GET_PIN_IDX];

    /* Given values the lookup overwrites: the compiler cannot see through the
     * call, so -Wmaybe-uninitialized fires without them. */
    timer_instance_t instance = TIMER_TIM2;
    timer_channel_t channel   = TIMER_CH1;

    status_t status_ret = resolve_channel(port, pin, &instance, &channel);

    if (status_ret != STATUS_OK)
    {
        return status_ret;
    }

    return timer_pwm_get_duty(instance, channel, duty_permille);
}

status_t pwm_controller_group_get(const uint8_t *payload, uint8_t length, uint32_t *frequency_hz)
{
    if (payload == NULL)
    {
        LOG_ERROR(MODULE_NAME, "payload argument is null");
        return STATUS_ERR_INVALID_ARG;
    }

    if (length != PWM_GROUP_GET_PAYLOAD_LEN)
    {
        LOG_ERROR(MODULE_NAME, "invalid length %u, expected %u", length, PWM_GROUP_GET_PAYLOAD_LEN);
        return STATUS_ERR_INVALID_ARG;
    }

    if (frequency_hz == NULL)
    {
        LOG_ERROR(MODULE_NAME, "frequency output argument is null");
        return STATUS_ERR_INVALID_ARG;
    }

    uint8_t group = payload[PWM_GROUP_GET_GROUP_IDX];

    if (group >= TIMER_INSTANCE_COUNT)
    {
        LOG_ERROR(MODULE_NAME, "invalid group %u", group);
        return STATUS_ERR_INVALID_ARG;
    }

    return timer_get_frequency((timer_instance_t)group, frequency_hz);
}
