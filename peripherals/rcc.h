#ifndef RCC_H
#define RCC_H

#include <stdint.h>
#include "status.h"

/**
 * @brief Selectable system clock (SYSCLK) targets.
 */
typedef enum
{
    RCC_SYSCLK_HSI_170MHZ = 0U, /**< 170 MHz from HSI16 via PLL (Range 1 boost) */
} rcc_sysclk_t;

/**
 * @brief Peripherals whose bus clock is gated through the RCC module.
 *
 * This is the only handle other drivers use to enable or disable a clock;
 * the mapping to RCC enable registers and bits lives entirely in rcc.c.
 */
typedef enum
{
    RCC_PERIPH_GPIOA = 0U,
    RCC_PERIPH_GPIOB,
    RCC_PERIPH_GPIOC,
    RCC_PERIPH_GPIOD,
    RCC_PERIPH_GPIOE,
    RCC_PERIPH_GPIOF,
    RCC_PERIPH_GPIOG,
    RCC_PERIPH_SYSCFG,
    RCC_PERIPH_USART1,
    RCC_PERIPH_USART2,
    RCC_PERIPH_USART3,
    RCC_PERIPH_UART4,
    RCC_PERIPH_UART5,
    RCC_PERIPH_LPUART1,
    RCC_PERIPH_COUNT,
} rcc_periph_t;

/**
 * @brief Kernel clock source for peripherals with a selectable clock (CCIPR).
 *
 * Values match the 2-bit USARTxSEL/LPUART1SEL field encoding.
 */
typedef enum
{
    RCC_CLK_SRC_PCLK   = 0U, /**< APB peripheral clock */
    RCC_CLK_SRC_SYSCLK = 1U, /**< System clock */
    RCC_CLK_SRC_HSI16  = 2U, /**< 16 MHz internal oscillator */
    RCC_CLK_SRC_LSE    = 3U, /**< Low-speed external oscillator */
} rcc_clk_src_t;

/**
 * @brief Configure the system clock to the requested target.
 *
 * For ::RCC_SYSCLK_HSI_170MHZ the sequence is, in order:
 *   1. Enable the PWR clock, select voltage scaling Range 1 and boost mode.
 *   2. Set flash latency to 4 wait states (with prefetch and caches) before
 *      raising the frequency.
 *   3. Enable HSI16 and configure the PLL (M=4, N=85, R=2 -> 170 MHz).
 *   4. Switch SYSCLK to the PLL with the AHB prescaler stepped /2 then /1.
 *
 * Every hardware ready-flag wait is bounded by a retry counter, so a failure
 * returns an error instead of hanging.
 *
 * @param target Desired system clock configuration
 * @return STATUS_OK on success, STATUS_ERR_INVALID_ARG on unknown target,
 *         STATUS_ERR_TIMEOUT if a hardware ready-flag wait expires
 */
status_t rcc_init(rcc_sysclk_t target);

/**
 * @brief Get the currently configured SYSCLK frequency.
 *
 * @return SYSCLK frequency in Hz, or 0 if rcc_init() has not succeeded
 */
uint32_t rcc_get_sysclk_hz(void);

/**
 * @brief Enable the bus clock for a peripheral.
 *
 * A dummy read-back guarantees the clock is active before the caller
 * accesses the peripheral.
 *
 * @param periph Peripheral to clock
 * @return STATUS_OK on success, STATUS_ERR_INVALID_ARG if @p periph is out of range
 */
status_t rcc_periph_enable(rcc_periph_t periph);

/**
 * @brief Disable the bus clock for a peripheral.
 *
 * @param periph Peripheral to gate off
 * @return STATUS_OK on success, STATUS_ERR_INVALID_ARG if @p periph is out of range
 */
status_t rcc_periph_disable(rcc_periph_t periph);

/**
 * @brief Select the kernel clock source for a peripheral (CCIPR).
 *
 * Only peripherals with a selectable clock (the USART/UART/LPUART
 * instances) are supported.
 *
 * @param periph Peripheral to configure
 * @param src    Desired clock source
 * @return STATUS_OK on success, STATUS_ERR_UNSUPPORTED if @p periph has no selectable clock
 */
status_t rcc_periph_set_clock_source(rcc_periph_t periph, rcc_clk_src_t src);

#endif /* RCC_H */
