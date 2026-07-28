#ifndef GPIO_SPY_H
#define GPIO_SPY_H

#include "gpio.h"

/* Clears every recorded call and return-value override back to defaults.
 * Call from each test's setup() so tests don't leak state into each other. */
void GpioSpy_Reset(void);

/* --- control what the spy returns --- */
void GpioSpy_SetReturnStatus(status_t status); /* applies to every status_t-returning call */
void GpioSpy_SetIsInput(bool value);
void GpioSpy_SetIsOutput(bool value);
void GpioSpy_SetIsAf(bool value);
void GpioSpy_SetReadState(bool value); /* value gpio_read() writes to its output param */

/* --- inspect what was last recorded --- */
gpio_pin_t GpioSpy_GetLastInitPin(void);
gpio_config_t GpioSpy_GetLastInitConfig(void);

gpio_pin_t GpioSpy_GetLastDeinitPin(void);

gpio_pin_t GpioSpy_GetLastSetStatePin(void);
gpio_state_t GpioSpy_GetLastSetStateState(void);

gpio_pin_t GpioSpy_GetLastTogglePin(void);

gpio_pin_t GpioSpy_GetLastReadPin(void);

gpio_pin_t GpioSpy_GetLastInitInterruptPin(void);
gpio_irq_config_t GpioSpy_GetLastInitInterruptConfig(void);

gpio_pin_t GpioSpy_GetLastDeinitInterruptPin(void);

gpio_pin_t GpioSpy_GetLastSetAfPin(void);
gpio_af_t GpioSpy_GetLastSetAfAf(void);

#endif /* GPIO_SPY_H */
