#ifndef RCC_H
#define RCC_H

#include <stdint.h>

/**
 * @brief Selectable system clock (SYSCLK) targets.
 */
typedef enum {
    RCC_SYSCLK_HSI_170MHZ = 0U, /**< 170 MHz from HSI16 via PLL (Range 1 boost) */
} rcc_sysclk_t;

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
 * @return 0 on success, -1 on invalid target or hardware timeout
 */
int rcc_init(rcc_sysclk_t target);

/**
 * @brief Get the currently configured SYSCLK frequency.
 *
 * @return SYSCLK frequency in Hz, or 0 if rcc_init() has not succeeded
 */
uint32_t rcc_get_sysclk_hz(void);

#endif /* RCC_H */
