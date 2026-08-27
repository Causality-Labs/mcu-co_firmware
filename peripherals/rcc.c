#include <stdint.h>
#include <stdbool.h>
#include "stm32g474xx.h"
#include "rcc.h"
#include "status.h"

/*
 * PLL settings for 170 MHz SYSCLK from the 16 MHz HSI16:
 *   VCO_in  = HSI16 / PLLM  = 16 MHz / 4  = 4 MHz    (must be 2.66 - 16 MHz)
 *   VCO_out = VCO_in * PLLN = 4 MHz * 85  = 340 MHz  (must be 96 - 344 MHz)
 *   SYSCLK  = VCO_out / PLLR = 340 MHz / 2 = 170 MHz
 *
 * Register encodings: PLLM field = M - 1, PLLN field = N, PLLR field = R/2 - 1.
 */
#define RCC_PLL_M 4U
#define RCC_PLL_N 85U
#define RCC_PLL_R 2U

#define RCC_SYSCLK_HSI_170MHZ_HZ 170000000U

/* Wait states for HCLK in (136, 170] MHz, Range 1 boost (RM0440 Table 9). */
#define RCC_FLASH_LATENCY_170MHZ FLASH_ACR_LATENCY_4WS

/* Bounded retry budget for hardware ready-flag polling. */
#define RCC_READY_TIMEOUT 100000U

/* Settling delay (loop iterations) between the /2 and /1 AHB prescaler steps. */
#define RCC_BOOST_SETTLE_LOOPS 100U

/* Width of a CCIPR clock-source selection field. */
#define RCC_CLK_SRC_MASK 3U

static uint32_t rcc_sysclk_hz = 0U;

/* Maps each peripheral to its RCC enable register and bit. */
typedef struct
{
    volatile uint32_t *enr;
    uint32_t bit;
} rcc_periph_clk_t;

static const rcc_periph_clk_t rcc_periph_clk[RCC_PERIPH_COUNT] = {
    [RCC_PERIPH_GPIOA]   = {&RCC->AHB2ENR, RCC_AHB2ENR_GPIOAEN},
    [RCC_PERIPH_GPIOB]   = {&RCC->AHB2ENR, RCC_AHB2ENR_GPIOBEN},
    [RCC_PERIPH_GPIOC]   = {&RCC->AHB2ENR, RCC_AHB2ENR_GPIOCEN},
    [RCC_PERIPH_GPIOD]   = {&RCC->AHB2ENR, RCC_AHB2ENR_GPIODEN},
    [RCC_PERIPH_GPIOE]   = {&RCC->AHB2ENR, RCC_AHB2ENR_GPIOEEN},
    [RCC_PERIPH_GPIOF]   = {&RCC->AHB2ENR, RCC_AHB2ENR_GPIOFEN},
    [RCC_PERIPH_GPIOG]   = {&RCC->AHB2ENR, RCC_AHB2ENR_GPIOGEN},
    [RCC_PERIPH_SYSCFG]  = {&RCC->APB2ENR, RCC_APB2ENR_SYSCFGEN},
    [RCC_PERIPH_USART1]  = {&RCC->APB2ENR, RCC_APB2ENR_USART1EN},
    [RCC_PERIPH_USART2]  = {&RCC->APB1ENR1, RCC_APB1ENR1_USART2EN},
    [RCC_PERIPH_USART3]  = {&RCC->APB1ENR1, RCC_APB1ENR1_USART3EN},
    [RCC_PERIPH_UART4]   = {&RCC->APB1ENR1, RCC_APB1ENR1_UART4EN},
    [RCC_PERIPH_UART5]   = {&RCC->APB1ENR1, RCC_APB1ENR1_UART5EN},
    [RCC_PERIPH_LPUART1] = {&RCC->APB1ENR2, RCC_APB1ENR2_LPUART1EN},
    [RCC_PERIPH_TIM2]    = {&RCC->APB1ENR1, RCC_APB1ENR1_TIM2EN},
    [RCC_PERIPH_TIM3]    = {&RCC->APB1ENR1, RCC_APB1ENR1_TIM3EN},
    [RCC_PERIPH_TIM4]    = {&RCC->APB1ENR1, RCC_APB1ENR1_TIM4EN},
};

/**
 * @brief Poll a register field until it matches an expected value, bounded.
 *
 * @param reg      Register to read
 * @param mask     Bits to compare
 * @param expected Required value of the masked bits
 * @return 0 once (reg & mask) == expected, -1 if the retry budget expires
 */
static int rcc_wait_value(const volatile uint32_t *reg, uint32_t mask, uint32_t expected)
{
    uint32_t retries = RCC_READY_TIMEOUT;
    while ((*reg & mask) != expected)
    {
        if (retries == 0U)
        {
            return -1;
        }
        retries--;
    }
    return 0;
}

/**
 * @brief Select Range 1 voltage scaling and enable boost mode.
 *
 * Boost mode (PWR_CR5.R1MODE = 0) is required for HCLK above 150 MHz.
 *
 * @return 0 on success, -1 if the regulator does not become ready in time
 */
static int rcc_enable_boost(void)
{
    RCC->APB1ENR1 |= RCC_APB1ENR1_PWREN;
    (void)RCC->APB1ENR1; /* dummy read so the PWR clock is active before access */

    uint32_t cr1 = PWR->CR1;
    cr1 &= ~PWR_CR1_VOS;
    cr1 |= PWR_CR1_VOS_0; /* 0b01 = Range 1 */
    PWR->CR1 = cr1;

    if (rcc_wait_value(&PWR->SR2, PWR_SR2_VOSF, 0U) != 0)
    {
        return -1;
    }

    PWR->CR5 &= ~PWR_CR5_R1MODE; /* clear R1MODE -> Range 1 boost */

    return 0;
}

/**
 * @brief Set flash latency, prefetch and caches for 170 MHz operation.
 *
 * Must run before raising the clock so the flash always has enough wait
 * states. The latency is read back to confirm it was accepted.
 *
 * @return 0 on success, -1 if the latency did not take effect
 */
static int rcc_configure_flash(void)
{
    uint32_t acr = FLASH->ACR;
    acr &= ~FLASH_ACR_LATENCY;
    acr |= RCC_FLASH_LATENCY_170MHZ | FLASH_ACR_PRFTEN | FLASH_ACR_ICEN | FLASH_ACR_DCEN;
    FLASH->ACR = acr;

    if ((FLASH->ACR & FLASH_ACR_LATENCY) != RCC_FLASH_LATENCY_170MHZ)
    {
        return -1;
    }

    return 0;
}

/**
 * @brief Enable HSI16 and bring up the PLL at 170 MHz.
 *
 * The PLL is disabled before reconfiguration, as PLLCFGR can only be written
 * while the PLL is off.
 *
 * @return 0 on success, -1 if HSI16 or the PLL fail to lock in time
 */
static int rcc_start_pll(void)
{
    RCC->CR |= RCC_CR_HSION;
    if (rcc_wait_value(&RCC->CR, RCC_CR_HSIRDY, RCC_CR_HSIRDY) != 0)
    {
        return -1;
    }

    RCC->CR &= ~RCC_CR_PLLON;
    if (rcc_wait_value(&RCC->CR, RCC_CR_PLLRDY, 0U) != 0)
    {
        return -1;
    }

    RCC->PLLCFGR = RCC_PLLCFGR_PLLSRC_HSI | (((uint32_t)RCC_PLL_M - 1U) << RCC_PLLCFGR_PLLM_Pos) |
                   ((uint32_t)RCC_PLL_N << RCC_PLLCFGR_PLLN_Pos) |
                   (((uint32_t)RCC_PLL_R / 2U - 1U) << RCC_PLLCFGR_PLLR_Pos) |
                   RCC_PLLCFGR_PLLREN; /* enable the PLLR (SYSCLK) output */

    RCC->CR |= RCC_CR_PLLON;
    if (rcc_wait_value(&RCC->CR, RCC_CR_PLLRDY, RCC_CR_PLLRDY) != 0)
    {
        return -1;
    }

    return 0;
}

/**
 * @brief Switch SYSCLK to the PLL using the boost-mode prescaler stepping.
 *
 * Above 150 MHz the AHB prescaler is set to /2 across the clock switch to
 * limit the current peak, then restored to /1 after a short settle. APB1 and
 * APB2 run at /1 (both can take the full 170 MHz on the G4).
 *
 * @return 0 on success, -1 if SYSCLK does not switch to the PLL in time
 */
static int rcc_switch_to_pll(void)
{
    uint32_t cfgr = RCC->CFGR;
    cfgr &= ~(RCC_CFGR_HPRE | RCC_CFGR_PPRE1 | RCC_CFGR_PPRE2);
    cfgr |= RCC_CFGR_HPRE_DIV2; /* /2 during the switch (HCLK <= 150 MHz) */
    RCC->CFGR = cfgr;

    cfgr &= ~RCC_CFGR_SW;
    cfgr |= RCC_CFGR_SW_PLL;
    RCC->CFGR = cfgr;
    if (rcc_wait_value(&RCC->CFGR, RCC_CFGR_SWS, RCC_CFGR_SWS_PLL) != 0)
    {
        return -1;
    }

    for (volatile uint32_t i = 0U; i < RCC_BOOST_SETTLE_LOOPS; i++)
    {
    }

    RCC->CFGR = (RCC->CFGR & ~RCC_CFGR_HPRE) | RCC_CFGR_HPRE_DIV1; /* restore full speed */

    return 0;
}

status_t rcc_init(rcc_sysclk_t target)
{
    if (target != RCC_SYSCLK_HSI_170MHZ)
    {
        return STATUS_ERR_INVALID_ARG;
    }

    if (rcc_enable_boost() != 0)
    {
        return STATUS_ERR_TIMEOUT;
    }

    if (rcc_configure_flash() != 0)
    {
        return STATUS_ERR_TIMEOUT;
    }

    if (rcc_start_pll() != 0)
    {
        return STATUS_ERR_TIMEOUT;
    }

    if (rcc_switch_to_pll() != 0)
    {
        return STATUS_ERR_TIMEOUT;
    }

    rcc_sysclk_hz   = RCC_SYSCLK_HSI_170MHZ_HZ;
    SystemCoreClock = RCC_SYSCLK_HSI_170MHZ_HZ;

    return STATUS_OK;
}

uint32_t rcc_get_sysclk_hz(void)
{
    return rcc_sysclk_hz;
}

status_t rcc_periph_enable(rcc_periph_t periph)
{
    if (periph >= RCC_PERIPH_COUNT)
    {
        return STATUS_ERR_INVALID_ARG;
    }

    *rcc_periph_clk[periph].enr |= rcc_periph_clk[periph].bit;
    (void)*rcc_periph_clk[periph].enr; /* read-back so the clock is active */

    return STATUS_OK;
}

status_t rcc_periph_disable(rcc_periph_t periph)
{
    if (periph >= RCC_PERIPH_COUNT)
    {
        return STATUS_ERR_INVALID_ARG;
    }

    *rcc_periph_clk[periph].enr &= ~rcc_periph_clk[periph].bit;

    return STATUS_OK;
}

uint32_t rcc_get_timer_clk_hz(rcc_periph_t periph)
{
    switch (periph)
    {
    case RCC_PERIPH_TIM2:
    case RCC_PERIPH_TIM3:
    case RCC_PERIPH_TIM4:
        return rcc_sysclk_hz;

    default:
        return 0U;
    }
}

status_t rcc_periph_set_clock_source(rcc_periph_t periph, rcc_clk_src_t src)
{
    uint32_t pos;

    switch (periph)
    {
    case RCC_PERIPH_USART1:
        pos = RCC_CCIPR_USART1SEL_Pos;
        break;
    case RCC_PERIPH_USART2:
        pos = RCC_CCIPR_USART2SEL_Pos;
        break;
    case RCC_PERIPH_USART3:
        pos = RCC_CCIPR_USART3SEL_Pos;
        break;
    case RCC_PERIPH_UART4:
        pos = RCC_CCIPR_UART4SEL_Pos;
        break;
    case RCC_PERIPH_UART5:
        pos = RCC_CCIPR_UART5SEL_Pos;
        break;
    case RCC_PERIPH_LPUART1:
        pos = RCC_CCIPR_LPUART1SEL_Pos;
        break;
    default:
        return STATUS_ERR_UNSUPPORTED;
    }

    RCC->CCIPR = (RCC->CCIPR & ~(RCC_CLK_SRC_MASK << pos)) | ((uint32_t)src << pos);

    return STATUS_OK;
}
