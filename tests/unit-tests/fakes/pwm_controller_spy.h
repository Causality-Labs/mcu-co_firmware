#ifndef PWM_CONTROLLER_SPY_H
#define PWM_CONTROLLER_SPY_H

#include "pwm_controller.h"

#define PWM_CONTROLLER_SPY_MAX_PAYLOAD 16U

typedef enum
{
    PWM_CONTROLLER_CALL_NONE,
    PWM_CONTROLLER_CALL_GROUP_CFG,
    PWM_CONTROLLER_CALL_GROUP_RELEASE,
    PWM_CONTROLLER_CALL_GROUP_GET,
    PWM_CONTROLLER_CALL_CHANNEL_CFG,
    PWM_CONTROLLER_CALL_CHANNEL_SET,
    PWM_CONTROLLER_CALL_CHANNEL_RELEASE,
    PWM_CONTROLLER_CALL_CHANNEL_GET,
} pwm_controller_call_t;

/* Clears the recorded call and return-value overrides back to defaults.
 * Call from each test's setup(). */
void PwmControllerSpy_Reset(void);

/* --- control what the spy returns --- */
void PwmControllerSpy_SetReturnStatus(status_t status);    /* applies to every status_t-returning call */
void PwmControllerSpy_SetDuty(uint16_t duty_permille);     /* value pwm_controller_channel_get() writes out */
void PwmControllerSpy_SetFrequency(uint32_t frequency_hz); /* value pwm_controller_group_get() writes out */

/* --- inspect the last call --- */
pwm_controller_call_t PwmControllerSpy_GetLastCall(void);
const uint8_t *PwmControllerSpy_GetLastPayload(void);
uint8_t PwmControllerSpy_GetLastLength(void);

#endif /* PWM_CONTROLLER_SPY_H */
