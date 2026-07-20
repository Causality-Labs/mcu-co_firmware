#ifndef GPIO_IRQ_BINDINGS_H
#define GPIO_IRQ_BINDINGS_H

#include <stdbool.h>
#include "gpio.h"

/* Matches the wire ACTION field 1:1 (mcu-co_Protocol.md: low=0, high=1, toggle=2). */
typedef enum {
    IRQ_ACTION_LOW    = 0U,
    IRQ_ACTION_HIGH   = 1U,
    IRQ_ACTION_TOGGLE = 2U,
} irq_action_t;

typedef struct {
    gpio_pin_t input_pin;
    gpio_pin_t output_pin;
    irq_action_t action;
    gpio_trigger_t trigger;
    bool active;
} irq_binding_t;

/* Indexed by pin number (0-15), matching how EXTI lines are shared across ports. */
extern irq_binding_t irq_bindings[MAX_GPIO_INTERRUPTS];

/* irq_dispatch_table[pin] is the callback to hand gpio_init_interrupt() - it
 * looks up irq_bindings[pin] at fire time, so it's safe to install before
 * anything is actually bound. */
extern const gpio_irq_callback_t irq_dispatch_table[MAX_GPIO_INTERRUPTS];

#endif /* GPIO_IRQ_BINDINGS_H */
