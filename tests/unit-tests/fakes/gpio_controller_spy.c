/*
 * Link-time substitute for src/gpio_controller.c (see gpio_controller.h).
 * Records which function was last called and the raw payload/length it was
 * given, instead of touching any real GPIO. Linked instead of the real
 * gpio_controller.c when building command_dispatcher's tests.
 */
#include <string.h>
#include "gpio_controller_spy.h"

static gpio_controller_call_t last_call;
static uint8_t last_payload[GPIO_CONTROLLER_SPY_MAX_PAYLOAD];
static uint8_t last_length;

static bool read_return_state;
static status_t forced_status;

void GpioControllerSpy_Reset(void)
{
    last_call = GPIO_CONTROLLER_CALL_NONE;
    memset(last_payload, 0, sizeof(last_payload));
    last_length = 0;

    read_return_state = false;
    forced_status      = STATUS_OK;
}

void GpioControllerSpy_SetReturnStatus(status_t status)
{
    forced_status = status;
}

void GpioControllerSpy_SetReadState(bool value)
{
    read_return_state = value;
}

gpio_controller_call_t GpioControllerSpy_GetLastCall(void)
{
    return last_call;
}

const uint8_t *GpioControllerSpy_GetLastPayload(void)
{
    return last_payload;
}

uint8_t GpioControllerSpy_GetLastLength(void)
{
    return last_length;
}

static void record_call(gpio_controller_call_t call, const uint8_t *payload, uint8_t length)
{
    last_call   = call;
    last_length = length;

    uint8_t copy_len = length;
    if (copy_len > sizeof(last_payload))
    {
        copy_len = sizeof(last_payload);
    }

    memset(last_payload, 0, sizeof(last_payload));
    if (payload != NULL)
    {
        memcpy(last_payload, payload, copy_len);
    }
}

/* --- gpio_controller.h implementation --- */

status_t gpio_controller_io_cfg(const uint8_t *payload, uint8_t length)
{
    record_call(GPIO_CONTROLLER_CALL_IO_CFG, payload, length);
    return forced_status;
}

status_t gpio_controller_write(const uint8_t *payload, uint8_t length)
{
    record_call(GPIO_CONTROLLER_CALL_WRITE, payload, length);
    return forced_status;
}

status_t gpio_controller_read(const uint8_t *payload, uint8_t length, bool *state)
{
    record_call(GPIO_CONTROLLER_CALL_READ, payload, length);

    if (state != NULL)
    {
        *state = read_return_state;
    }

    return forced_status;
}

status_t gpio_controller_irq_cfg(const uint8_t *payload, uint8_t length)
{
    record_call(GPIO_CONTROLLER_CALL_IRQ_CFG, payload, length);
    return forced_status;
}

status_t gpio_controller_irq_bind(const uint8_t *payload, uint8_t length)
{
    record_call(GPIO_CONTROLLER_CALL_IRQ_BIND, payload, length);
    return forced_status;
}

status_t gpio_controller_irq_unbind(const uint8_t *payload, uint8_t length)
{
    record_call(GPIO_CONTROLLER_CALL_IRQ_UNBIND, payload, length);
    return forced_status;
}
