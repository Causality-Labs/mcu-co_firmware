#ifndef TIMER_H
#define TIMER_H

#include <stdint.h>
#include "gpio.h"
#include "status.h"

/** @brief Duty cycle is expressed in tenths of a percent: 0 = 0%, 1000 = 100.0%. */
#define TIMER_DUTY_MAX 1000U

/*
 * Frequency bounds. The lower bound is where a 16-bit prescaler and a 16-bit
 * reload stop being able to divide the timer clock far enough; the upper bound
 * is where the reload gets so small that duty resolution collapses to a few
 * steps.
 */
#define TIMER_FREQ_MIN_HZ 1U
#define TIMER_FREQ_MAX_HZ 1000000U

/**
 * @brief Timers exposed by this driver.
 *
 * General-purpose timers only. The advanced timers (TIM1/TIM8/TIM20) are
 * excluded: their outputs stay dead unless BDTR.MOE is set, and their dead-time
 * and break machinery buys nothing for single-ended PWM.
 */
typedef enum
{
    TIMER_TIM2 = 0U,
    TIMER_TIM3,
    TIMER_TIM4,
    TIMER_INSTANCE_COUNT,
} timer_instance_t;

/** @brief Output compare channels available on each timer. */
typedef enum
{
    TIMER_CH1 = 0U,
    TIMER_CH2,
    TIMER_CH3,
    TIMER_CH4,
    TIMER_CHANNEL_COUNT,
} timer_channel_t;

/** @brief Output level during the active part of the PWM period. */
typedef enum
{
    TIMER_POLARITY_ACTIVE_HIGH = 0U,
    TIMER_POLARITY_ACTIVE_LOW = 1U,
} timer_polarity_t;

/** @brief PWM output configuration for a single channel. */
typedef struct
{
    uint16_t duty_permille;
    timer_polarity_t polarity;
} timer_pwm_config_t;

/**
 * @brief Initialise a timer's time base.
 *
 * Enables the timer clock and programs the prescaler and reload for the
 * requested frequency. The counter is left stopped and no channel output is
 * configured; use timer_start() and timer_pwm_channel_init() for those.
 *
 * @param instance     Timer to initialise
 * @param frequency_hz Time-base frequency, TIMER_FREQ_MIN_HZ to TIMER_FREQ_MAX_HZ
 * @return STATUS_OK on success, STATUS_ERR_INVALID_ARG on an invalid instance or
 *         an out-of-range frequency, STATUS_ERR_BUSY if already initialised,
 *         STATUS_ERR_NOT_INIT if the timer clock is unavailable because
 *         rcc_init() has not succeeded.
 */
status_t timer_init(timer_instance_t instance, uint32_t frequency_hz);

/**
 * @brief Release a timer and every channel it drives.
 *
 * Stops the counter, deconfigures all channel outputs, releases each claimed
 * pin, and gates the timer clock off.
 *
 * @param instance Timer to release
 * @return STATUS_OK on success, STATUS_ERR_INVALID_ARG on an invalid instance,
 *         STATUS_ERR_NOT_INIT if the timer was not initialised.
 */
status_t timer_deinit(timer_instance_t instance);

/**
 * @brief Start the counter.
 *
 * Separate from timer_init() so several channels can be configured while the
 * counter is stopped and then begin together on a single call.
 *
 * @param instance Timer to start
 * @return STATUS_OK on success, STATUS_ERR_INVALID_ARG on an invalid instance,
 *         STATUS_ERR_NOT_INIT if the timer was not initialised.
 */
status_t timer_start(timer_instance_t instance);

/**
 * @brief Stop the counter, leaving the configuration intact.
 *
 * Channel outputs hold the level they had when the counter stopped.
 *
 * @param instance Timer to stop
 * @return STATUS_OK on success, STATUS_ERR_INVALID_ARG on an invalid instance,
 *         STATUS_ERR_NOT_INIT if the timer was not initialised.
 */
status_t timer_stop(timer_instance_t instance);

/**
 * @brief Change a timer's frequency.
 *
 * The prescaler and reload are shared by all four channels, so this affects
 * every channel on @p instance. The duty cycle of each configured channel is
 * preserved: compare values are recomputed against the new reload, since a raw
 * compare value means a different duty once the reload changes.
 *
 * @param instance     Timer to retune
 * @param frequency_hz New frequency, TIMER_FREQ_MIN_HZ to TIMER_FREQ_MAX_HZ
 * @return STATUS_OK on success, STATUS_ERR_INVALID_ARG on an invalid instance or
 *         an out-of-range frequency, STATUS_ERR_NOT_INIT if not initialised.
 */
status_t timer_set_frequency(timer_instance_t instance, uint32_t frequency_hz);

/**
 * @brief Read back a timer's frequency.
 *
 * Reports the frequency the hardware actually produces, which may differ
 * slightly from the requested value because the prescaler and reload are
 * integers.
 *
 * @param instance     Timer to query
 * @param frequency_hz Output parameter for the achieved frequency in Hz
 * @return STATUS_OK on success, STATUS_ERR_INVALID_ARG on an invalid instance or
 *         a NULL pointer, STATUS_ERR_NOT_INIT if not initialised.
 */
status_t timer_get_frequency(timer_instance_t instance, uint32_t *frequency_hz);

/**
 * @brief Configure a channel as a PWM output and claim its pin.
 *
 * Configures the mapped GPIO as an alternate function, programs PWM mode with
 * compare preload, and enables the channel output. Output begins once the
 * counter is running. The pin is refused if another driver already owns it.
 *
 * @param instance Timer owning the channel
 * @param channel  Channel to configure
 * @param config   Pointer to PWM output configuration
 * @return STATUS_OK on success, STATUS_ERR_INVALID_ARG on an invalid instance,
 *         channel, NULL @p config, or out-of-range duty, STATUS_ERR_NOT_INIT if
 *         the timer is not initialised, STATUS_ERR_BUSY if the channel is
 *         already configured or its pin is in use.
 */
status_t timer_pwm_channel_init(timer_instance_t instance, timer_channel_t channel, const timer_pwm_config_t *config);

/**
 * @brief Deconfigure a PWM channel and release its pin.
 *
 * @param instance Timer owning the channel
 * @param channel  Channel to release
 * @return STATUS_OK on success, STATUS_ERR_INVALID_ARG on an invalid instance or
 *         channel, STATUS_ERR_NOT_INIT if the channel is not configured.
 */
status_t timer_pwm_channel_deinit(timer_instance_t instance, timer_channel_t channel);

/**
 * @brief Update a channel's duty cycle.
 *
 * Writes the compare register only, so the frequency and every other channel
 * are untouched. The write is buffered by the hardware and takes effect at the
 * next period boundary, so it can never produce a partial pulse.
 *
 * @param instance      Timer owning the channel
 * @param channel       Channel to update
 * @param duty_permille New duty, 0 to TIMER_DUTY_MAX
 * @return STATUS_OK on success, STATUS_ERR_INVALID_ARG on an invalid instance,
 *         channel, or duty, STATUS_ERR_NOT_INIT if the channel is not configured.
 */
status_t timer_pwm_set_duty(timer_instance_t instance, timer_channel_t channel, uint16_t duty_permille);

/**
 * @brief Read back a channel's duty cycle.
 *
 * @param instance      Timer owning the channel
 * @param channel       Channel to query
 * @param duty_permille Output parameter for the duty in tenths of a percent
 * @return STATUS_OK on success, STATUS_ERR_INVALID_ARG on an invalid instance,
 *         channel, or a NULL pointer, STATUS_ERR_NOT_INIT if the channel is not
 *         configured.
 */
status_t timer_pwm_get_duty(timer_instance_t instance, timer_channel_t channel, uint16_t *duty_permille);

/**
 * @brief Resolve a GPIO pin to the timer and channel that can drive it.
 *
 * The pin-to-channel map lives in timer.c alongside the alternate-function
 * table, so callers never encode the mapping themselves.
 *
 * @param pin      Pin to look up
 * @param instance Output parameter for the owning timer
 * @param channel  Output parameter for the owning channel
 * @return STATUS_OK on success, STATUS_ERR_INVALID_ARG if any pointer is NULL,
 *         STATUS_ERR_UNSUPPORTED if the pin has no PWM channel mapped.
 */
status_t timer_pwm_lookup_pin(const gpio_pin_t *pin, timer_instance_t *instance, timer_channel_t *channel);

#endif /* TIMER_H */
