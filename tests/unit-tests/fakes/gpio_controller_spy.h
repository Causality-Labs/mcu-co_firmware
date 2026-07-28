#ifndef GPIO_CONTROLLER_SPY_H
#define GPIO_CONTROLLER_SPY_H

#include "gpio_controller.h"

#define GPIO_CONTROLLER_SPY_MAX_PAYLOAD 16U

typedef enum
{
    GPIO_CONTROLLER_CALL_NONE,
    GPIO_CONTROLLER_CALL_IO_CFG,
    GPIO_CONTROLLER_CALL_WRITE,
    GPIO_CONTROLLER_CALL_READ,
    GPIO_CONTROLLER_CALL_IRQ_CFG,
    GPIO_CONTROLLER_CALL_IRQ_BIND,
    GPIO_CONTROLLER_CALL_IRQ_UNBIND,
} gpio_controller_call_t;

/* Clears the recorded call and return-value overrides back to defaults.
 * Call from each test's setup(). */
void GpioControllerSpy_Reset(void);

/* --- control what the spy returns --- */
void GpioControllerSpy_SetReturnStatus(status_t status); /* applies to every status_t-returning call */
void GpioControllerSpy_SetReadState(bool value);          /* value gpio_controller_read() writes to its output param */

/* --- inspect the last call --- */
gpio_controller_call_t GpioControllerSpy_GetLastCall(void);
const uint8_t *GpioControllerSpy_GetLastPayload(void);
uint8_t GpioControllerSpy_GetLastLength(void);

#endif /* GPIO_CONTROLLER_SPY_H */
