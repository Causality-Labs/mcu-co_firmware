/*
 * Link-time substitute for peripherals/timer.c (see timer.h). Implements the
 * same function signatures but records the order calls arrived in, and the
 * arguments of the last call carrying each one, instead of touching real
 * registers. Linked instead of the real timer.c when building pwm_controller's
 * tests.
 *
 * Unlike gpio_spy, this one records a sequence rather than only a dead drop
 * per function: pwm_controller's group setup is order-dependent (init then
 * start, and a rollback deinit on failure), and "which calls, in what order"
 * is exactly what those tests assert.
 */
#include <string.h>
#include "timer_spy.h"

static timer_spy_call_t call_sequence[TIMER_SPY_MAX_CALLS];
static uint8_t call_count;

static status_t forced_status[TIMER_CALL_COUNT];

static uint32_t reported_frequency;
static uint16_t reported_duty;
static timer_instance_t lookup_instance;
static timer_channel_t lookup_channel;

static timer_instance_t last_instance;
static timer_channel_t last_channel;
static uint32_t last_frequency;
static uint16_t last_duty;
static timer_polarity_t last_polarity;
static gpio_pin_t last_lookup_pin;

void TimerSpy_Reset(void)
{
    memset(call_sequence, 0, sizeof(call_sequence));
    call_count = 0U;

    for (uint8_t i = 0U; i < (uint8_t)TIMER_CALL_COUNT; i++)
    {
        forced_status[i] = STATUS_OK;
    }

    reported_frequency = 0U;
    reported_duty      = 0U;
    lookup_instance    = TIMER_TIM2;
    lookup_channel     = TIMER_CH1;

    last_instance  = TIMER_TIM2;
    last_channel   = TIMER_CH1;
    last_frequency = 0U;
    last_duty      = 0U;
    last_polarity  = TIMER_POLARITY_ACTIVE_HIGH;
    memset(&last_lookup_pin, 0, sizeof(last_lookup_pin));
}

void TimerSpy_SetReturnStatus(status_t status)
{
    for (uint8_t i = 0U; i < (uint8_t)TIMER_CALL_COUNT; i++)
    {
        forced_status[i] = status;
    }
}

void TimerSpy_SetReturnStatusFor(timer_spy_call_t call, status_t status)
{
    if ((call <= TIMER_CALL_NONE) || (call >= TIMER_CALL_COUNT))
    {
        return;
    }

    forced_status[call] = status;
}

void TimerSpy_SetFrequency(uint32_t frequency_hz)
{
    reported_frequency = frequency_hz;
}

void TimerSpy_SetDuty(uint16_t duty_permille)
{
    reported_duty = duty_permille;
}

void TimerSpy_SetLookupResult(timer_instance_t instance, timer_channel_t channel)
{
    lookup_instance = instance;
    lookup_channel  = channel;
}

uint8_t TimerSpy_GetCallCount(void)
{
    return call_count;
}

timer_spy_call_t TimerSpy_GetCall(uint8_t index)
{
    if (index >= call_count)
    {
        return TIMER_CALL_NONE;
    }

    return call_sequence[index];
}

timer_spy_call_t TimerSpy_GetLastCall(void)
{
    if (call_count == 0U)
    {
        return TIMER_CALL_NONE;
    }

    return call_sequence[call_count - 1U];
}

uint8_t TimerSpy_CountCalls(timer_spy_call_t call)
{
    uint8_t matches = 0U;

    for (uint8_t i = 0U; i < call_count; i++)
    {
        if (call_sequence[i] == call)
        {
            matches++;
        }
    }

    return matches;
}

timer_instance_t TimerSpy_GetLastInstance(void)
{
    return last_instance;
}

timer_channel_t TimerSpy_GetLastChannel(void)
{
    return last_channel;
}

uint32_t TimerSpy_GetLastFrequency(void)
{
    return last_frequency;
}

uint16_t TimerSpy_GetLastDuty(void)
{
    return last_duty;
}

timer_polarity_t TimerSpy_GetLastPolarity(void)
{
    return last_polarity;
}

gpio_pin_t TimerSpy_GetLastLookupPin(void)
{
    return last_lookup_pin;
}

static status_t record_call(timer_spy_call_t call)
{
    if (call_count < TIMER_SPY_MAX_CALLS)
    {
        call_sequence[call_count] = call;
        call_count++;
    }

    return forced_status[call];
}

/* --- timer.h implementation --- */

status_t timer_init(timer_instance_t instance, uint32_t frequency_hz)
{
    last_instance  = instance;
    last_frequency = frequency_hz;

    return record_call(TIMER_CALL_INIT);
}

status_t timer_deinit(timer_instance_t instance)
{
    last_instance = instance;

    return record_call(TIMER_CALL_DEINIT);
}

status_t timer_start(timer_instance_t instance)
{
    last_instance = instance;

    return record_call(TIMER_CALL_START);
}

status_t timer_stop(timer_instance_t instance)
{
    last_instance = instance;

    return record_call(TIMER_CALL_STOP);
}

status_t timer_set_frequency(timer_instance_t instance, uint32_t frequency_hz)
{
    last_instance  = instance;
    last_frequency = frequency_hz;

    return record_call(TIMER_CALL_SET_FREQUENCY);
}

status_t timer_get_frequency(timer_instance_t instance, uint32_t *frequency_hz)
{
    last_instance = instance;

    status_t status = record_call(TIMER_CALL_GET_FREQUENCY);

    /* Only written on success, so a controller that ignores the status and
     * forwards the output parameter anyway is caught by the test. */
    if ((status == STATUS_OK) && (frequency_hz != NULL))
    {
        *frequency_hz = reported_frequency;
    }

    return status;
}

status_t timer_pwm_channel_init(timer_instance_t instance, timer_channel_t channel, const timer_pwm_config_t *config)
{
    last_instance = instance;
    last_channel  = channel;

    if (config != NULL)
    {
        last_duty     = config->duty_permille;
        last_polarity = config->polarity;
    }

    return record_call(TIMER_CALL_PWM_CHANNEL_INIT);
}

status_t timer_pwm_channel_deinit(timer_instance_t instance, timer_channel_t channel)
{
    last_instance = instance;
    last_channel  = channel;

    return record_call(TIMER_CALL_PWM_CHANNEL_DEINIT);
}

status_t timer_pwm_set_duty(timer_instance_t instance, timer_channel_t channel, uint16_t duty_permille)
{
    last_instance = instance;
    last_channel  = channel;
    last_duty     = duty_permille;

    return record_call(TIMER_CALL_PWM_SET_DUTY);
}

status_t timer_pwm_get_duty(timer_instance_t instance, timer_channel_t channel, uint16_t *duty_permille)
{
    last_instance = instance;
    last_channel  = channel;

    status_t status = record_call(TIMER_CALL_PWM_GET_DUTY);

    if ((status == STATUS_OK) && (duty_permille != NULL))
    {
        *duty_permille = reported_duty;
    }

    return status;
}

status_t timer_pwm_lookup_pin(const gpio_pin_t *pin, timer_instance_t *instance, timer_channel_t *channel)
{
    if (pin != NULL)
    {
        last_lookup_pin = *pin;
    }

    status_t status = record_call(TIMER_CALL_LOOKUP_PIN);

    if (status == STATUS_OK)
    {
        if (instance != NULL)
        {
            *instance = lookup_instance;
        }

        if (channel != NULL)
        {
            *channel = lookup_channel;
        }
    }

    return status;
}
