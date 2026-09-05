#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "stm32g474xx.h"
#include "rcc.h"
#include "status.h"
#include "timer.h"

/* PSC and ARR are 16-bit on every timer this driver exposes. TIM2's ARR is
 * physically 32-bit, but capping it keeps one time-base calculation valid for
 * all three instances instead of a per-instance width. */
#define TIMER_PRESCALER_MAX 65535U
#define TIMER_RELOAD_MAX    65535U

/* OCxM selects the compare mode. It is a 4-bit field split across the register:
 * three bits at the field base and a fourth twelve places above it (RM0440
 * 30.5.10, "Bits 16, 6:4"), so it must be cleared with the full split mask. */
#define TIMER_OCM_FIELD     0x1007U
#define TIMER_OCM_PWM_MODE1 0x0006U
#define TIMER_CCS_FIELD     0x3U

typedef struct
{
    gpio_pin_t pin;
    gpio_af_t af;
} timer_pin_t;

static TIM_TypeDef *const timer_regs[TIMER_INSTANCE_COUNT] = {
    [TIMER_TIM2] = TIM2,
    [TIMER_TIM3] = TIM3,
    [TIMER_TIM4] = TIM4,
};

static const rcc_periph_t timer_rcc_periph[TIMER_INSTANCE_COUNT] = {
    [TIMER_TIM2] = RCC_PERIPH_TIM2,
    [TIMER_TIM3] = RCC_PERIPH_TIM3,
    [TIMER_TIM4] = RCC_PERIPH_TIM4,
};

static bool timer_initialized[TIMER_INSTANCE_COUNT] = {
    [TIMER_TIM2] = false,
    [TIMER_TIM3] = false,
    [TIMER_TIM4] = false,
};

static const timer_pin_t timer_pins[TIMER_INSTANCE_COUNT][TIMER_CHANNEL_COUNT] = {
    [TIMER_TIM2] = {[TIMER_CH1] = {{GPIO_PORT_A, 5U}, GPIO_AF1},
                    [TIMER_CH2] = {{GPIO_PORT_A, 1U}, GPIO_AF1},
                    [TIMER_CH3] = {{GPIO_PORT_B, 10U}, GPIO_AF1},
                    [TIMER_CH4] = {{GPIO_PORT_B, 11U}, GPIO_AF1}},
    [TIMER_TIM3] = {[TIMER_CH1] = {{GPIO_PORT_C, 6U}, GPIO_AF2},
                    [TIMER_CH2] = {{GPIO_PORT_C, 7U}, GPIO_AF2},
                    [TIMER_CH3] = {{GPIO_PORT_C, 8U}, GPIO_AF2},
                    [TIMER_CH4] = {{GPIO_PORT_C, 9U}, GPIO_AF2}},
    [TIMER_TIM4] = {[TIMER_CH1] = {{GPIO_PORT_B, 6U}, GPIO_AF2},
                    [TIMER_CH2] = {{GPIO_PORT_B, 7U}, GPIO_AF2},
                    [TIMER_CH3] = {{GPIO_PORT_B, 8U}, GPIO_AF2},
                    [TIMER_CH4] = {{GPIO_PORT_B, 9U}, GPIO_AF2}},
};

static bool timer_channel_configured[TIMER_INSTANCE_COUNT][TIMER_CHANNEL_COUNT];

/* The requested duty, kept because the compare register cannot give it back:
 * the conversion to a compare value truncates, so re-deriving a duty from CCR
 * on every retune would let the error accumulate instead of staying a one-off
 * rounding of the original request. */
static uint16_t timer_duty_permille[TIMER_INSTANCE_COUNT][TIMER_CHANNEL_COUNT];

/**
 * @brief Split a target frequency into prescaler and reload values.
 *
 * Picks the smallest prescaler that brings the period within the 16-bit reload,
 * which leaves the reload as large as possible and so the duty resolution as
 * fine as possible.
 *
 * @param clk_hz       Timer kernel clock
 * @param frequency_hz Desired output frequency
 * @param psc          Output parameter for the prescaler register value
 * @param arr          Output parameter for the reload register value
 * @return STATUS_OK on success, STATUS_ERR_INVALID_ARG if the frequency cannot
 *         be reached from @p clk_hz within 16-bit prescaler and reload.
 */
static status_t timer_compute_timebase(uint32_t clk_hz, uint32_t frequency_hz, uint16_t *psc, uint16_t *arr)
{
    uint32_t total = clk_hz / frequency_hz;

    /* One tick for the reload plus one for a compare to land under it. */
    if (total < 2U)
    {
        return STATUS_ERR_INVALID_ARG;
    }

    uint32_t prescaler = (total - 1U) / (TIMER_RELOAD_MAX + 1U);
    if (prescaler > TIMER_PRESCALER_MAX)
    {
        return STATUS_ERR_INVALID_ARG;
    }

    uint32_t reload = (total / (prescaler + 1U)) - 1U;

    /* A null reload blocks the counter entirely (RM0440 30.5.15). */
    if ((reload == 0U) || (reload > TIMER_RELOAD_MAX))
    {
        return STATUS_ERR_INVALID_ARG;
    }

    *psc = (uint16_t)prescaler;
    *arr = (uint16_t)reload;

    return STATUS_OK;
}

/**
 * @brief Position of a channel's OCxM field within its CCMRx register.
 *
 * OCxPE sits one bit below this position and CCxS four bits below it, so this
 * single value locates every output-compare field for the channel.
 */
static uint32_t timer_ocm_pos(timer_channel_t channel)
{
    return ((channel == TIMER_CH1) || (channel == TIMER_CH3)) ? 4U : 12U;
}

/** @brief CCMR1 holds channels 1 and 2, CCMR2 holds channels 3 and 4. */
static volatile uint32_t *timer_ccmr(TIM_TypeDef *timer, timer_channel_t channel)
{
    return (channel <= TIMER_CH2) ? &timer->CCMR1 : &timer->CCMR2;
}

static volatile uint32_t *timer_ccr(TIM_TypeDef *timer, timer_channel_t channel)
{
    volatile uint32_t *ccr;

    switch (channel)
    {
    case TIMER_CH1:
        ccr = &timer->CCR1;
        break;
    case TIMER_CH2:
        ccr = &timer->CCR2;
        break;
    case TIMER_CH3:
        ccr = &timer->CCR3;
        break;
    default:
        ccr = &timer->CCR4;
        break;
    }

    return ccr;
}

/**
 * @brief Convert a duty cycle to a compare value for the current reload.
 *
 * A compare value above the reload holds the output high for the whole period,
 * which is how the hardware expresses 100% (RM0440 30.4.11).
 */
static uint32_t timer_duty_to_compare(uint32_t arr, uint16_t duty_permille)
{
    uint32_t period = arr + 1U;

    if (duty_permille >= TIMER_DUTY_MAX)
    {
        return period;
    }

    return (period * duty_permille) / TIMER_DUTY_MAX;
}

/**
 * @brief Load pending preload values into the shadow registers if safe.
 *
 * PSC, ARR and the compare registers are all buffered, so a write only reaches
 * the hardware on an update event. A running timer raises one at the end of the
 * current period, which is what makes a change glitch-free. A stopped timer
 * never will, so force the event there and only there: UG also resets the
 * counter, which would cut short the period of any channel already driving a pin.
 */
static void timer_load_shadow_if_stopped(TIM_TypeDef *timer)
{
    if ((timer->CR1 & TIM_CR1_CEN) == 0U)
    {
        timer->EGR = TIM_EGR_UG;
        timer->SR  = 0U; /* UG raises the update flag; clear it so it reads clean */
    }
}

status_t timer_init(timer_instance_t instance, uint32_t frequency_hz)
{
    if (instance >= TIMER_INSTANCE_COUNT)
    {
        return STATUS_ERR_INVALID_ARG;
    }

    if ((frequency_hz < TIMER_FREQ_MIN_HZ) || (frequency_hz > TIMER_FREQ_MAX_HZ))
    {
        return STATUS_ERR_INVALID_ARG;
    }

    if (timer_initialized[instance])
    {
        return STATUS_ERR_BUSY;
    }

    uint32_t clk_hz = rcc_get_timer_clk_hz(timer_rcc_periph[instance]);
    if (clk_hz == 0U)
    {
        return STATUS_ERR_NOT_INIT;
    }

    uint16_t psc = 0U;
    uint16_t arr = 0U;

    status_t status = timer_compute_timebase(clk_hz, frequency_hz, &psc, &arr);
    if (status != STATUS_OK)
    {
        return status;
    }

    if (rcc_periph_enable(timer_rcc_periph[instance]) != STATUS_OK)
    {
        return STATUS_ERR_NOT_INIT;
    }

    TIM_TypeDef *timer = timer_regs[instance];

    timer->CR1  = TIM_CR1_ARPE; /* counter off, upcounting, reload buffered */
    timer->CCER = 0U;
    timer->PSC  = psc;
    timer->ARR  = arr;
    timer->CNT  = 0U;

    /* Without an update event the first period would run on reset values
     * rather than the prescaler and reload just written (RM0440 30.4.11). */
    timer_load_shadow_if_stopped(timer);

    timer_initialized[instance] = true;

    return STATUS_OK;
}

status_t timer_deinit(timer_instance_t instance)
{
    if (instance >= TIMER_INSTANCE_COUNT)
    {
        return STATUS_ERR_INVALID_ARG;
    }

    if (!timer_initialized[instance])
    {
        return STATUS_ERR_NOT_INIT;
    }

    /* Release the channels first: they own GPIO pins, which outlive the timer
     * registers and would stay stuck in alternate-function mode otherwise. */
    for (uint32_t c = 0U; c < (uint32_t)TIMER_CHANNEL_COUNT; c++)
    {
        if (timer_channel_configured[instance][c])
        {
            (void)timer_pwm_channel_deinit(instance, (timer_channel_t)c);
        }
    }

    TIM_TypeDef *timer = timer_regs[instance];

    timer->CR1  = 0U;
    timer->CCER = 0U;
    timer->PSC  = 0U;
    timer->ARR  = TIMER_RELOAD_MAX; /* reset value */
    timer->CNT  = 0U;
    timer->SR   = 0U;

    (void)rcc_periph_disable(timer_rcc_periph[instance]);

    timer_initialized[instance] = false;

    return STATUS_OK;
}

status_t timer_start(timer_instance_t instance)
{
    if (instance >= TIMER_INSTANCE_COUNT)
    {
        return STATUS_ERR_INVALID_ARG;
    }

    if (!timer_initialized[instance])
    {
        return STATUS_ERR_NOT_INIT;
    }

    timer_regs[instance]->CR1 |= TIM_CR1_CEN;

    return STATUS_OK;
}

status_t timer_stop(timer_instance_t instance)
{
    if (instance >= TIMER_INSTANCE_COUNT)
    {
        return STATUS_ERR_INVALID_ARG;
    }

    if (!timer_initialized[instance])
    {
        return STATUS_ERR_NOT_INIT;
    }

    timer_regs[instance]->CR1 &= ~TIM_CR1_CEN;

    return STATUS_OK;
}

status_t timer_get_frequency(timer_instance_t instance, uint32_t *frequency_hz)
{
    if (instance >= TIMER_INSTANCE_COUNT)
    {
        return STATUS_ERR_INVALID_ARG;
    }

    if (frequency_hz == NULL)
    {
        return STATUS_ERR_INVALID_ARG;
    }

    if (!timer_initialized[instance])
    {
        return STATUS_ERR_NOT_INIT;
    }

    uint32_t clk_hz = rcc_get_timer_clk_hz(timer_rcc_periph[instance]);
    if (clk_hz == 0U)
    {
        return STATUS_ERR_NOT_INIT;
    }

    const TIM_TypeDef *timer = timer_regs[instance];

    /* Read back the registers rather than the requested frequency: the integer
     * prescaler and reload rarely divide out to exactly what was asked for. */
    uint32_t period = (timer->PSC + 1U) * (timer->ARR + 1U);

    *frequency_hz = clk_hz / period;

    return STATUS_OK;
}

status_t timer_set_frequency(timer_instance_t instance, uint32_t frequency_hz)
{
    if (instance >= TIMER_INSTANCE_COUNT)
    {
        return STATUS_ERR_INVALID_ARG;
    }

    if ((frequency_hz < TIMER_FREQ_MIN_HZ) || (frequency_hz > TIMER_FREQ_MAX_HZ))
    {
        return STATUS_ERR_INVALID_ARG;
    }

    if (!timer_initialized[instance])
    {
        return STATUS_ERR_NOT_INIT;
    }

    uint32_t clk_hz = rcc_get_timer_clk_hz(timer_rcc_periph[instance]);
    if (clk_hz == 0U)
    {
        return STATUS_ERR_NOT_INIT;
    }

    uint16_t psc = 0U;
    uint16_t arr = 0U;

    /* Compute before touching the registers so a rejected frequency leaves the
     * running time base exactly as it was. */
    status_t status = timer_compute_timebase(clk_hz, frequency_hz, &psc, &arr);
    if (status != STATUS_OK)
    {
        return status;
    }

    TIM_TypeDef *timer = timer_regs[instance];

    timer->PSC = psc;
    timer->ARR = arr;

    /* A compare value means a different duty once the reload changes, so every
     * configured channel has to be recomputed against the new one. */
    for (uint32_t c = 0U; c < (uint32_t)TIMER_CHANNEL_COUNT; c++)
    {
        if (timer_channel_configured[instance][c])
        {
            timer_channel_t channel = (timer_channel_t)c;

            *timer_ccr(timer, channel) = timer_duty_to_compare(arr, timer_duty_permille[instance][c]);
        }
    }

    /* Reload and compares are all preloaded, so the whole group switches to the
     * new time base on one update event rather than drifting channel by channel. */
    timer_load_shadow_if_stopped(timer);

    return STATUS_OK;
}

status_t timer_pwm_channel_init(timer_instance_t instance, timer_channel_t channel, const timer_pwm_config_t *config)
{
    if ((instance >= TIMER_INSTANCE_COUNT) || (channel >= TIMER_CHANNEL_COUNT))
    {
        return STATUS_ERR_INVALID_ARG;
    }

    if (config == NULL)
    {
        return STATUS_ERR_INVALID_ARG;
    }

    if (config->duty_permille > TIMER_DUTY_MAX)
    {
        return STATUS_ERR_INVALID_ARG;
    }

    if ((config->polarity != TIMER_POLARITY_ACTIVE_HIGH) && (config->polarity != TIMER_POLARITY_ACTIVE_LOW))
    {
        return STATUS_ERR_INVALID_ARG;
    }

    if (!timer_initialized[instance])
    {
        return STATUS_ERR_NOT_INIT;
    }

    if (timer_channel_configured[instance][channel])
    {
        return STATUS_ERR_BUSY;
    }

    const timer_pin_t *map = &timer_pins[instance][channel];

    if (is_pin_an_input(&map->pin) || is_pin_an_output(&map->pin) || is_pin_an_af(&map->pin))
    {
        return STATUS_ERR_BUSY;
    }

    const gpio_config_t af_config = {
        .mode  = GPIO_MODE_AF,
        .type  = GPIO_TYPE_PUSH_PULL,
        .speed = GPIO_SPEED_HIGH,
        .pull  = GPIO_PULL_NONE,
    };

    status_t status = gpio_init(&map->pin, &af_config);
    if (status != STATUS_OK)
    {
        return status;
    }

    status = gpio_set_af(&map->pin, map->af);
    if (status != STATUS_OK)
    {
        (void)gpio_deinit(&map->pin);
        return status;
    }

    TIM_TypeDef *timer      = timer_regs[instance];
    uint32_t ocm_pos        = timer_ocm_pos(channel);
    volatile uint32_t *ccmr = timer_ccmr(timer, channel);

    uint32_t mode = *ccmr;
    mode &= ~((TIMER_OCM_FIELD << ocm_pos) | (TIMER_CCS_FIELD << (ocm_pos - 4U)));
    mode |= (TIMER_OCM_PWM_MODE1 << ocm_pos); /* CCxS left at 00 = output */
    mode |= (1U << (ocm_pos - 1U));           /* OCxPE: buffer compare writes */
    *ccmr = mode;

    *timer_ccr(timer, channel) = timer_duty_to_compare(timer->ARR, config->duty_permille);

    uint32_t enable_bit   = 1U << (4U * (uint32_t)channel);
    uint32_t polarity_bit = 1U << ((4U * (uint32_t)channel) + 1U);

    uint32_t ccer = timer->CCER;
    ccer &= ~(enable_bit | polarity_bit);
    if (config->polarity == TIMER_POLARITY_ACTIVE_LOW)
    {
        ccer |= polarity_bit;
    }
    ccer |= enable_bit;
    timer->CCER = ccer;

    timer_load_shadow_if_stopped(timer);

    timer_duty_permille[instance][channel]      = config->duty_permille;
    timer_channel_configured[instance][channel] = true;

    return STATUS_OK;
}

status_t timer_pwm_channel_deinit(timer_instance_t instance, timer_channel_t channel)
{
    if ((instance >= TIMER_INSTANCE_COUNT) || (channel >= TIMER_CHANNEL_COUNT))
    {
        return STATUS_ERR_INVALID_ARG;
    }

    if (!timer_channel_configured[instance][channel])
    {
        return STATUS_ERR_NOT_INIT;
    }

    TIM_TypeDef *timer      = timer_regs[instance];
    uint32_t ocm_pos        = timer_ocm_pos(channel);
    volatile uint32_t *ccmr = timer_ccmr(timer, channel);

    timer->CCER &= ~((1U << (4U * (uint32_t)channel)) | (1U << ((4U * (uint32_t)channel) + 1U)));
    *ccmr &= ~((TIMER_OCM_FIELD << ocm_pos) | (1U << (ocm_pos - 1U)));
    *timer_ccr(timer, channel) = 0U;

    (void)gpio_deinit(&timer_pins[instance][channel].pin);

    timer_duty_permille[instance][channel]      = 0U;
    timer_channel_configured[instance][channel] = false;

    return STATUS_OK;
}

status_t timer_pwm_set_duty(timer_instance_t instance, timer_channel_t channel, uint16_t duty_permille)
{
    if ((instance >= TIMER_INSTANCE_COUNT) || (channel >= TIMER_CHANNEL_COUNT))
    {
        return STATUS_ERR_INVALID_ARG;
    }

    if (duty_permille > TIMER_DUTY_MAX)
    {
        return STATUS_ERR_INVALID_ARG;
    }

    if (!timer_channel_configured[instance][channel])
    {
        return STATUS_ERR_NOT_INIT;
    }

    TIM_TypeDef *timer = timer_regs[instance];

    /* ARR reads back the preload register, so a retune still waiting on its
     * update event is already accounted for here. */
    *timer_ccr(timer, channel) = timer_duty_to_compare(timer->ARR, duty_permille);

    timer_duty_permille[instance][channel] = duty_permille;

    timer_load_shadow_if_stopped(timer);

    return STATUS_OK;
}

status_t timer_pwm_get_duty(timer_instance_t instance, timer_channel_t channel, uint16_t *duty_permille)
{
    if ((instance >= TIMER_INSTANCE_COUNT) || (channel >= TIMER_CHANNEL_COUNT))
    {
        return STATUS_ERR_INVALID_ARG;
    }

    if (duty_permille == NULL)
    {
        return STATUS_ERR_INVALID_ARG;
    }

    if (!timer_channel_configured[instance][channel])
    {
        return STATUS_ERR_NOT_INIT;
    }

    *duty_permille = timer_duty_permille[instance][channel];

    return STATUS_OK;
}

status_t timer_pwm_lookup_pin(const gpio_pin_t *pin, timer_instance_t *instance, timer_channel_t *channel)
{
    if ((pin == NULL) || (instance == NULL) || (channel == NULL))
    {
        return STATUS_ERR_INVALID_ARG;
    }

    for (uint32_t i = 0U; i < (uint32_t)TIMER_INSTANCE_COUNT; i++)
    {
        for (uint32_t c = 0U; c < (uint32_t)TIMER_CHANNEL_COUNT; c++)
        {
            const gpio_pin_t *mapped = &timer_pins[i][c].pin;

            if ((mapped->port == pin->port) && (mapped->pin == pin->pin))
            {
                *instance = (timer_instance_t)i;
                *channel  = (timer_channel_t)c;
                return STATUS_OK;
            }
        }
    }

    return STATUS_ERR_UNSUPPORTED;
}
