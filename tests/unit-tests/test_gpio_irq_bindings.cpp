#include <cstring>
#include "CppUTest/TestHarness.h"

extern "C" {
#include "gpio_irq_bindings.h"
#include "gpio_spy.h"
}

TEST_GROUP(GpioIrqBindings)
{
    void setup() override
    {
        GpioSpy_Reset();
        memset(irq_bindings, 0, sizeof(irq_bindings));
    }
};

/* --- irq_dispatch_table / irq_action_dispatch --- */

// An inactive binding should be a no-op - no gpio_set_state() call recorded.
TEST(GpioIrqBindings, DispatchDoesNothingWhenBindingInactive)
{
    irq_bindings[3].active     = false;
    irq_bindings[3].action     = IRQ_ACTION_HIGH;
    irq_bindings[3].output_pin = (gpio_pin_t){GPIO_PORT_C, 9};

    irq_dispatch_table[3]();

    gpio_pin_t pin = GpioSpy_GetLastSetStatePin();
    LONGS_EQUAL(GPIO_PORT_A, pin.port); // still the post-reset default
    LONGS_EQUAL(0, pin.pin);
}

// IRQ_ACTION_LOW should drive the bound output pin low.
TEST(GpioIrqBindings, DispatchCallsGpioSetStateLowForLowAction)
{
    irq_bindings[3].active     = true;
    irq_bindings[3].action     = IRQ_ACTION_LOW;
    irq_bindings[3].output_pin = (gpio_pin_t){GPIO_PORT_B, 4};

    irq_dispatch_table[3]();

    gpio_pin_t pin = GpioSpy_GetLastSetStatePin();
    LONGS_EQUAL(GPIO_PORT_B, pin.port);
    LONGS_EQUAL(4, pin.pin);
    LONGS_EQUAL(GPIO_LOW, GpioSpy_GetLastSetStateState());
}

// IRQ_ACTION_HIGH should drive the bound output pin high.
TEST(GpioIrqBindings, DispatchCallsGpioSetStateHighForHighAction)
{
    irq_bindings[3].active     = true;
    irq_bindings[3].action     = IRQ_ACTION_HIGH;
    irq_bindings[3].output_pin = (gpio_pin_t){GPIO_PORT_B, 4};

    irq_dispatch_table[3]();

    LONGS_EQUAL(GPIO_HIGH, GpioSpy_GetLastSetStateState());
}

// IRQ_ACTION_TOGGLE should toggle the bound output pin instead of calling
// gpio_set_state().
TEST(GpioIrqBindings, DispatchCallsGpioToggleForToggleAction)
{
    irq_bindings[3].active     = true;
    irq_bindings[3].action     = IRQ_ACTION_TOGGLE;
    irq_bindings[3].output_pin = (gpio_pin_t){GPIO_PORT_D, 2};

    irq_dispatch_table[3]();

    gpio_pin_t pin = GpioSpy_GetLastTogglePin();
    LONGS_EQUAL(GPIO_PORT_D, pin.port);
    LONGS_EQUAL(2, pin.pin);
}

// The dispatch table's first entry must act on line 0's binding, not some
// other line - guards against a copy-paste bug in the 16 boilerplate
// irq_dispatch_N() wrapper functions.
TEST(GpioIrqBindings, DispatchTableEntryZeroActsOnLine0Binding)
{
    irq_bindings[0].active     = true;
    irq_bindings[0].action     = IRQ_ACTION_HIGH;
    irq_bindings[0].output_pin = (gpio_pin_t){GPIO_PORT_F, 11};

    irq_dispatch_table[0]();

    gpio_pin_t pin = GpioSpy_GetLastSetStatePin();
    LONGS_EQUAL(GPIO_PORT_F, pin.port);
    LONGS_EQUAL(11, pin.pin);
}

// Same check at the other boundary - the last table entry must act on
// line 15's binding.
TEST(GpioIrqBindings, DispatchTableEntryFifteenActsOnLine15Binding)
{
    irq_bindings[15].active     = true;
    irq_bindings[15].action     = IRQ_ACTION_HIGH;
    irq_bindings[15].output_pin = (gpio_pin_t){GPIO_PORT_G, 15};

    irq_dispatch_table[15]();

    gpio_pin_t pin = GpioSpy_GetLastSetStatePin();
    LONGS_EQUAL(GPIO_PORT_G, pin.port);
    LONGS_EQUAL(15, pin.pin);
}
