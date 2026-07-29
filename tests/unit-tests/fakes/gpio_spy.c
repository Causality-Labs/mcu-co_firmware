/*
 * Link-time substitute for peripherals/gpio.c (see gpio.h). Implements the
 * exact same function signatures but records the arguments of the last call
 * to each function (the "dead drop") instead of touching real registers.
 * Linked instead of the real gpio.c when building gpio_controller's tests.
 */
#include <string.h>
#include "gpio_spy.h"

static gpio_pin_t last_init_pin;
static gpio_config_t last_init_config;

static gpio_pin_t last_deinit_pin;

static gpio_pin_t last_set_state_pin;
static gpio_state_t last_set_state_state;

static gpio_pin_t last_toggle_pin;

static gpio_pin_t last_read_pin;
static bool read_return_state;

static gpio_pin_t last_init_interrupt_pin;
static gpio_irq_config_t last_init_interrupt_config;

static gpio_pin_t last_deinit_interrupt_pin;

static gpio_pin_t last_set_af_pin;
static gpio_af_t last_set_af_af;

static bool is_input_return;
static bool is_output_return;
static bool is_af_return;

static status_t forced_status;

void GpioSpy_Reset(void)
{
    memset(&last_init_pin, 0, sizeof(last_init_pin));
    memset(&last_init_config, 0, sizeof(last_init_config));
    memset(&last_deinit_pin, 0, sizeof(last_deinit_pin));
    memset(&last_set_state_pin, 0, sizeof(last_set_state_pin));
    last_set_state_state = GPIO_LOW;
    memset(&last_toggle_pin, 0, sizeof(last_toggle_pin));
    memset(&last_read_pin, 0, sizeof(last_read_pin));
    read_return_state = false;
    memset(&last_init_interrupt_pin, 0, sizeof(last_init_interrupt_pin));
    memset(&last_init_interrupt_config, 0, sizeof(last_init_interrupt_config));
    memset(&last_deinit_interrupt_pin, 0, sizeof(last_deinit_interrupt_pin));
    memset(&last_set_af_pin, 0, sizeof(last_set_af_pin));
    last_set_af_af = GPIO_AF0;

    is_input_return  = true;
    is_output_return = true;
    is_af_return      = false;

    forced_status = STATUS_OK;
}

void GpioSpy_SetReturnStatus(status_t status)
{
    forced_status = status;
}

void GpioSpy_SetIsInput(bool value)
{
    is_input_return = value;
}

void GpioSpy_SetIsOutput(bool value)
{
    is_output_return = value;
}

void GpioSpy_SetIsAf(bool value)
{
    is_af_return = value;
}

void GpioSpy_SetReadState(bool value)
{
    read_return_state = value;
}

gpio_pin_t GpioSpy_GetLastInitPin(void)
{
    return last_init_pin;
}

gpio_config_t GpioSpy_GetLastInitConfig(void)
{
    return last_init_config;
}

gpio_pin_t GpioSpy_GetLastDeinitPin(void)
{
    return last_deinit_pin;
}

gpio_pin_t GpioSpy_GetLastSetStatePin(void)
{
    return last_set_state_pin;
}

gpio_state_t GpioSpy_GetLastSetStateState(void)
{
    return last_set_state_state;
}

gpio_pin_t GpioSpy_GetLastTogglePin(void)
{
    return last_toggle_pin;
}

gpio_pin_t GpioSpy_GetLastReadPin(void)
{
    return last_read_pin;
}

gpio_pin_t GpioSpy_GetLastInitInterruptPin(void)
{
    return last_init_interrupt_pin;
}

gpio_irq_config_t GpioSpy_GetLastInitInterruptConfig(void)
{
    return last_init_interrupt_config;
}

gpio_pin_t GpioSpy_GetLastDeinitInterruptPin(void)
{
    return last_deinit_interrupt_pin;
}

gpio_pin_t GpioSpy_GetLastSetAfPin(void)
{
    return last_set_af_pin;
}

gpio_af_t GpioSpy_GetLastSetAfAf(void)
{
    return last_set_af_af;
}

/* --- gpio.h implementation --- */

status_t gpio_init(const gpio_pin_t *gpio, const gpio_config_t *config)
{
    last_init_pin    = *gpio;
    last_init_config = *config;
    return forced_status;
}

status_t gpio_deinit(const gpio_pin_t *gpio)
{
    last_deinit_pin = *gpio;
    return forced_status;
}

status_t gpio_set(const gpio_pin_t *gpio)
{
    last_set_state_pin   = *gpio;
    last_set_state_state = GPIO_HIGH;
    return forced_status;
}

status_t gpio_reset(const gpio_pin_t *gpio)
{
    last_set_state_pin   = *gpio;
    last_set_state_state = GPIO_LOW;
    return forced_status;
}

status_t gpio_set_state(const gpio_pin_t *gpio, gpio_state_t state)
{
    last_set_state_pin   = *gpio;
    last_set_state_state = state;
    return forced_status;
}

status_t gpio_toggle(const gpio_pin_t *gpio)
{
    last_toggle_pin = *gpio;
    return forced_status;
}

status_t gpio_read(const gpio_pin_t *gpio, bool *state)
{
    last_read_pin = *gpio;
    *state         = read_return_state;
    return forced_status;
}

status_t gpio_init_interrupt(const gpio_pin_t *gpio, const gpio_irq_config_t *config)
{
    last_init_interrupt_pin    = *gpio;
    last_init_interrupt_config = *config;
    return forced_status;
}

status_t gpio_deinit_interrupt(const gpio_pin_t *gpio)
{
    last_deinit_interrupt_pin = *gpio;
    return forced_status;
}

bool is_pin_an_af(const gpio_pin_t *gpio)
{
    (void)gpio;
    return is_af_return;
}

bool is_pin_an_output(const gpio_pin_t *gpio)
{
    (void)gpio;
    return is_output_return;
}

bool is_pin_an_input(const gpio_pin_t *gpio)
{
    (void)gpio;
    return is_input_return;
}

status_t gpio_set_af(const gpio_pin_t *gpio, gpio_af_t af)
{
    last_set_af_pin = *gpio;
    last_set_af_af  = af;
    return forced_status;
}
