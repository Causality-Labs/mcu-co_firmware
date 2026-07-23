#include <stdint.h>
#include "gpio.h"
#include "gpio_irq_bindings.h"

irq_binding_t irq_bindings[MAX_GPIO_INTERRUPTS];

static void irq_action_dispatch(uint8_t line)
{
    if (irq_bindings[line].active == false)
    {
        return;
    }

    switch (irq_bindings[line].action)
    {
    case IRQ_ACTION_LOW:
        (void)gpio_set_state(&irq_bindings[line].output_pin, GPIO_LOW);
        break;

    case IRQ_ACTION_HIGH:
        (void)gpio_set_state(&irq_bindings[line].output_pin, GPIO_HIGH);
        break;

    case IRQ_ACTION_TOGGLE:
        (void)gpio_toggle(&irq_bindings[line].output_pin);
        break;

    default:
        break;
    }
}

static void irq_dispatch_0(void)
{
    irq_action_dispatch(0);
}

static void irq_dispatch_1(void)
{
    irq_action_dispatch(1);
}

static void irq_dispatch_2(void)
{
    irq_action_dispatch(2);
}

static void irq_dispatch_3(void)
{
    irq_action_dispatch(3);
}

static void irq_dispatch_4(void)
{
    irq_action_dispatch(4);
}

static void irq_dispatch_5(void)
{
    irq_action_dispatch(5);
}

static void irq_dispatch_6(void)
{
    irq_action_dispatch(6);
}

static void irq_dispatch_7(void)
{
    irq_action_dispatch(7);
}

static void irq_dispatch_8(void)
{
    irq_action_dispatch(8);
}

static void irq_dispatch_9(void)
{
    irq_action_dispatch(9);
}

static void irq_dispatch_10(void)
{
    irq_action_dispatch(10);
}

static void irq_dispatch_11(void)
{
    irq_action_dispatch(11);
}

static void irq_dispatch_12(void)
{
    irq_action_dispatch(12);
}

static void irq_dispatch_13(void)
{
    irq_action_dispatch(13);
}

static void irq_dispatch_14(void)
{
    irq_action_dispatch(14);
}

static void irq_dispatch_15(void)
{
    irq_action_dispatch(15);
}

const gpio_irq_callback_t irq_dispatch_table[MAX_GPIO_INTERRUPTS] = {
    irq_dispatch_0,  irq_dispatch_1,  irq_dispatch_2,  irq_dispatch_3,  irq_dispatch_4,  irq_dispatch_5,
    irq_dispatch_6,  irq_dispatch_7,  irq_dispatch_8,  irq_dispatch_9,  irq_dispatch_10, irq_dispatch_11,
    irq_dispatch_12, irq_dispatch_13, irq_dispatch_14, irq_dispatch_15,
};
