#ifndef TIMER_SPY_H
#define TIMER_SPY_H

#include "timer.h"

/* Longest sequence any controller path should produce is 3 (init, start, or a
 * rollback deinit). Calls past this are counted but not stored. */
#define TIMER_SPY_MAX_CALLS 8U

typedef enum
{
    TIMER_CALL_NONE = 0,
    TIMER_CALL_INIT,
    TIMER_CALL_DEINIT,
    TIMER_CALL_START,
    TIMER_CALL_STOP,
    TIMER_CALL_SET_FREQUENCY,
    TIMER_CALL_GET_FREQUENCY,
    TIMER_CALL_PWM_CHANNEL_INIT,
    TIMER_CALL_PWM_CHANNEL_DEINIT,
    TIMER_CALL_PWM_SET_DUTY,
    TIMER_CALL_PWM_GET_DUTY,
    TIMER_CALL_LOOKUP_PIN,
    TIMER_CALL_COUNT,
} timer_spy_call_t;

/* Clears the recorded sequence and every return-value override back to
 * defaults. Call from each test's setup() so tests don't leak into each other. */
void TimerSpy_Reset(void);

/* --- control what the spy returns --- */

/* Applies to every function. */
void TimerSpy_SetReturnStatus(status_t status);

/* Applies to one function, overriding the blanket status above. Needed for
 * paths that depend on two different outcomes in one call - e.g. timer_init()
 * reporting STATUS_ERR_BUSY while timer_set_frequency() succeeds. */
void TimerSpy_SetReturnStatusFor(timer_spy_call_t call, status_t status);

void TimerSpy_SetFrequency(uint32_t frequency_hz); /* what timer_get_frequency() reports */
void TimerSpy_SetDuty(uint16_t duty_permille);     /* what timer_pwm_get_duty() reports */

/* What timer_pwm_lookup_pin() resolves a pin to. Kept programmable rather than
 * duplicating timer.c's real pin map here: a spy that knows the map would let a
 * controller bug hide behind the spy agreeing with it. */
void TimerSpy_SetLookupResult(timer_instance_t instance, timer_channel_t channel);

/* --- inspect the call sequence --- */

uint8_t TimerSpy_GetCallCount(void);
timer_spy_call_t TimerSpy_GetCall(uint8_t index); /* TIMER_CALL_NONE when out of range */
timer_spy_call_t TimerSpy_GetLastCall(void);
uint8_t TimerSpy_CountCalls(timer_spy_call_t call);

/* --- inspect the arguments last passed --- */

timer_instance_t TimerSpy_GetLastInstance(void);
timer_channel_t TimerSpy_GetLastChannel(void);
uint32_t TimerSpy_GetLastFrequency(void); /* from timer_init() or timer_set_frequency() */
uint16_t TimerSpy_GetLastDuty(void);      /* from timer_pwm_channel_init()'s config or set_duty() */
timer_polarity_t TimerSpy_GetLastPolarity(void);
gpio_pin_t TimerSpy_GetLastLookupPin(void);

#endif /* TIMER_SPY_H */
