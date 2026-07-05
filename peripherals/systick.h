#ifndef SYSTICK_H
#define SYSTICK_H

#include <stdint.h>
#include "status.h"

/**
 * @brief Start the SysTick 1 ms time base.
 *
 * Configures the Cortex-M SysTick timer to interrupt every millisecond off the
 * current SYSCLK, and zeroes the millisecond counter. Must be called after
 * rcc_init() has succeeded, as the reload value is derived from
 * rcc_get_sysclk_hz().
 *
 * @return STATUS_OK on success, STATUS_ERR_NOT_INIT if rcc_init() has not
 *         succeeded, STATUS_ERR_INVALID_STATE if the reload value overflows
 *         the 24-bit SysTick counter.
 */
status_t systick_init(void);

/**
 * @brief Milliseconds elapsed since systick_init().
 *
 * Free-running and wraps after ~49.7 days. Intended for non-blocking timing:
 * `if ((systick_get_ms() - start) >= period) { ... }`.
 *
 * @return Millisecond tick count since the time base started.
 */
uint32_t systick_get_ms(void);

/**
 * @brief Block for at least @p ms milliseconds.
 *
 * Busy-waits on the SysTick millisecond counter. The wait is wrap-safe and
 * guarantees a minimum delay (a call may return up to one tick late if issued
 * just before a tick fires). Do not call before systick_init() or from an ISR
 * with priority equal to or higher than SysTick.
 *
 * @param ms Number of milliseconds to wait.
 */
void delay_ms(uint32_t ms);

#endif /* SYSTICK_H */
