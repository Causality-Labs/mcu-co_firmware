#include <stdint.h>
#include "stm32g474xx.h"
#include "rcc.h"
#include "systick.h"
#include "status.h"

#define SYSTICK_RATE_HZ 1000U /* 1 ms tick */

/* Free-running millisecond counter, owned by the SysTick interrupt. */
static volatile uint32_t systick_ms = 0U;

/* Overrides the weak SysTick_Handler in the startup file. Declared here because
 * the handler is invoked only through the vector table, so no header prototypes it. */
void SysTick_Handler(void);

status_t systick_init(void)
{
    uint32_t sysclk_hz = rcc_get_sysclk_hz();
    if (sysclk_hz == 0U) {
        return STATUS_ERR_NOT_INIT;
    }

    systick_ms = 0U;

    /* SysTick_Config programs the reload, clears the counter, sets the lowest
     * interrupt priority, and enables the timer and its interrupt. It returns
     * non-zero if (reload - 1) overflows the 24-bit counter. */
    if (SysTick_Config(sysclk_hz / SYSTICK_RATE_HZ) != 0U) {
        return STATUS_ERR_INVALID_STATE;
    }

    return STATUS_OK;
}

uint32_t systick_get_ms(void)
{
    return systick_ms;
}

void delay_ms(uint32_t ms)
{
    uint32_t start = systick_ms;

    /* Unsigned subtraction makes the comparison correct across counter wrap. */
    while ((systick_ms - start) < ms) {
        /* spin */
    }
}

void SysTick_Handler(void)
{
    systick_ms++;
}
