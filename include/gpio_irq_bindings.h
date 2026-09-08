#ifndef GPIO_IRQ_BINDINGS_H
#define GPIO_IRQ_BINDINGS_H

#include <stdbool.h>
#include "gpio.h"

/** @brief Matches the wire ACTION field 1:1 (mcu-co_Protocol.md: low=0, high=1, toggle=2). */
typedef enum
{
    IRQ_ACTION_LOW    = 0U,
    IRQ_ACTION_HIGH   = 1U,
    IRQ_ACTION_TOGGLE = 2U,
} irq_action_t;

/** @brief What an armed input pin's interrupt does to an output pin. */
typedef struct
{
    gpio_pin_t input_pin;
    gpio_pin_t output_pin;
    irq_action_t action;
    bool active;
} irq_binding_t;

/**
 * @brief The binding in force for each pin, or a zeroed entry for none.
 *
 * Indexed by pin number (0-15): one slot covers PA5, PB5 and PC5 alike, matching
 * the hardware, which only lets one port own an EXTI line at a time.
 */
extern irq_binding_t irq_bindings[MAX_GPIO_INTERRUPTS];

/**
 * @brief Per-pin ISR callbacks to hand gpio_init_interrupt().
 *
 * Each looks irq_bindings[pin] up at fire time, so installing one before
 * anything is bound is safe - it does nothing until a binding exists.
 */
extern const gpio_irq_callback_t irq_dispatch_table[MAX_GPIO_INTERRUPTS];

#endif /* GPIO_IRQ_BINDINGS_H */
