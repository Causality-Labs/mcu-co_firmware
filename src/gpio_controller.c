#include <stddef.h>
#include <stdint.h>
#include "logger.h"
#include "gpio.h"
#include "gpio_controller.h"
#include "gpio_irq_bindings.h"
#include "status.h"

#define MODULE_NAME "GPIO_CONTROLLER"

#define GPIO_IRQ_DEFAULT_PRIORITY 5U

#define GPIO_CFG_PAYLOAD_LEN 3U
#define GPIO_CFG_DIR_IDX     0U
#define GPIO_CFG_PORT_IDX    1U
#define GPIO_CFG_PIN_IDX     2U

#define GPIO_WRITE_PAYLOAD_LEN 3U
#define GPIO_WRITE_LEVEL_IDX   0U
#define GPIO_WRITE_PORT_IDX    1U
#define GPIO_WRITE_PIN_IDX     2U

#define GPIO_READ_PAYLOAD_LEN 2U
#define GPIO_READ_PORT_IDX    0U
#define GPIO_READ_PIN_IDX     1U

#define GPIO_CFG_IRQ_PAYLOAD_LEN 3U
#define GPIO_CFG_IRQ_EDGE_IDX     0U
#define GPIO_CFG_IRQ_PORT_IDX    1U
#define GPIO_CFG_IRQ_PIN_IDX     2U

#define GPIO_IRQ_EDGE_OFF     0U
#define GPIO_IRQ_EDGE_RISING  1U
#define GPIO_IRQ_EDGE_FALLING 2U
#define GPIO_IRQ_EDGE_BOTH    3U

#define GPIO_IRQ_BIND_PAYLOAD_LEN  6U
#define GPIO_IRQ_BIND_EDGE_IDX     0U
#define GPIO_IRQ_BIND_IN_PORT_IDX  1U
#define GPIO_IRQ_BIND_IN_PIN_IDX   2U
#define GPIO_IRQ_BIND_ACTION_IDX   3U
#define GPIO_IRQ_BIND_OUT_PORT_IDX 4U
#define GPIO_IRQ_BIND_OUT_PIN_IDX  5U

#define GPIO_IRQ_UNBIND_PAYLOAD_LEN 2U
#define GPIO_IRQ_UNBIND_PORT_IDX    0U
#define GPIO_IRQ_UNBIND_PIN_IDX     1U


static status_t wire_edge_to_trigger(const uint8_t edge, gpio_trigger_t *trigger)
{
    if (trigger == NULL)
    {
        return STATUS_ERR_INVALID_ARG;
    }

    switch (edge)
    {
        case GPIO_IRQ_EDGE_RISING:
            *trigger = RISING;
            return STATUS_OK;

        case GPIO_IRQ_EDGE_FALLING:
            *trigger = FALLING;
            return STATUS_OK;

        case GPIO_IRQ_EDGE_BOTH:
            *trigger = BOTH;
            return STATUS_OK;

        default:
            return STATUS_ERR_INVALID_ARG;

    }
}

status_t gpio_controller_irq_cfg(const uint8_t *payload, uint8_t length)
{
    if (payload == NULL)
    {
        LOG_ERROR(MODULE_NAME, "payload argument is null");
        return STATUS_ERR_INVALID_ARG;
    }

    if (length != GPIO_CFG_IRQ_PAYLOAD_LEN)
    {
        LOG_ERROR(MODULE_NAME, "Invalid length");
        return STATUS_ERR_INVALID_ARG;
    }

    uint8_t edge = payload[GPIO_CFG_IRQ_EDGE_IDX];
    uint8_t port = payload[GPIO_CFG_IRQ_PORT_IDX];
    uint8_t pin  = payload[GPIO_CFG_IRQ_PIN_IDX];

    if (port >= GPIO_NUM_OF_PORTS)
    {
        return STATUS_ERR_INVALID_PIN;
    }

    if (pin > MAX_PIN_COUNT)
    {
        return STATUS_ERR_INVALID_PIN;
    }

    const gpio_pin_t gpio_pin = {
        .port = (gpio_port_t)port,
        .pin = pin
    };

    if (edge == GPIO_IRQ_EDGE_OFF)
    {
        if (gpio_deinit_interrupt(&gpio_pin) != STATUS_OK)
        {
            LOG_ERROR(MODULE_NAME, "Failed to disarm interrupt.");
            return STATUS_ERR;
        }

        irq_bindings[pin] = (irq_binding_t){0};

        return STATUS_OK;
    }

    gpio_trigger_t trigger;

    if (wire_edge_to_trigger(edge, &trigger) != STATUS_OK)
    {
        LOG_ERROR(MODULE_NAME, "Failed to wire edge to trigger.");
        return STATUS_ERR_INVALID_ARG;
    }

    const gpio_irq_config_t irq_cfg = {
        .trigger  = trigger,
        .callback = irq_dispatch_table[pin],
        .priority = GPIO_IRQ_DEFAULT_PRIORITY,
    };

    if (gpio_init_interrupt(&gpio_pin, &irq_cfg) != STATUS_OK)
    {
        LOG_ERROR(MODULE_NAME, "button IRQ init failed");
        return STATUS_ERR;
    }

    irq_bindings[pin] = (irq_binding_t){0};

    return STATUS_OK;
}

status_t gpio_controller_irq_bind(const uint8_t *payload, uint8_t length)
{
    if (payload == NULL)
    {
        LOG_ERROR(MODULE_NAME, "payload argument is null");
        return STATUS_ERR_INVALID_ARG;
    }

    if (length != GPIO_IRQ_BIND_PAYLOAD_LEN)
    {
        LOG_ERROR(MODULE_NAME, "Invalid length");
        return STATUS_ERR_INVALID_ARG;
    }

    uint8_t edge     = payload[GPIO_IRQ_BIND_EDGE_IDX];
    uint8_t in_port  = payload[GPIO_IRQ_BIND_IN_PORT_IDX];
    uint8_t in_pin   = payload[GPIO_IRQ_BIND_IN_PIN_IDX];
    uint8_t action   = payload[GPIO_IRQ_BIND_ACTION_IDX];
    uint8_t out_port = payload[GPIO_IRQ_BIND_OUT_PORT_IDX];
    uint8_t out_pin  = payload[GPIO_IRQ_BIND_OUT_PIN_IDX];

    if ((in_port >= GPIO_NUM_OF_PORTS) || (in_pin > MAX_PIN_COUNT)
        || (out_port >= GPIO_NUM_OF_PORTS) || (out_pin > MAX_PIN_COUNT))
    {
        return STATUS_ERR_INVALID_PIN;
    }

    if ((edge != GPIO_IRQ_EDGE_RISING) && (edge != GPIO_IRQ_EDGE_FALLING) && (edge != GPIO_IRQ_EDGE_BOTH))
    {
        LOG_ERROR(MODULE_NAME, "Invalid edge select");
        return STATUS_ERR_INVALID_ARG;
    }

    if ((action != (uint8_t)IRQ_ACTION_LOW) && (action != (uint8_t)IRQ_ACTION_HIGH)
        && (action != (uint8_t)IRQ_ACTION_TOGGLE))
    {
        return STATUS_ERR_INVALID_ARG;
    }

    const gpio_pin_t in_gpio = {
        .port = (gpio_port_t)in_port,
        .pin  = in_pin,
    };

    const gpio_pin_t out_gpio = {
        .port = (gpio_port_t)out_port,
        .pin  = out_pin,
    };

    if (!is_pin_an_input(&in_gpio) || !is_pin_an_output(&out_gpio))
    {
        LOG_ERROR(MODULE_NAME, "Pins not configured for irq bind");
        return STATUS_ERR_INVALID_STATE;
    }

    /* Refuse to silently replace an existing binding - the host must
     * gpio_controller_irq_unbind() this pin first. */
    if (irq_bindings[in_pin].active == true)
    {
        LOG_ERROR(MODULE_NAME, "Pin already bound - unbind first");
        return STATUS_ERR_BUSY;
    }

    irq_bindings[in_pin] = (irq_binding_t){
        .input_pin  = in_gpio,
        .output_pin = out_gpio,
        .action     = (irq_action_t)action,
        .active     = true,
    };

    return STATUS_OK;
}

status_t gpio_controller_irq_unbind(const uint8_t *payload, uint8_t length)
{
    if (payload == NULL)
    {
        LOG_ERROR(MODULE_NAME, "payload argument is null");
        return STATUS_ERR_INVALID_ARG;
    }

    if (length != GPIO_IRQ_UNBIND_PAYLOAD_LEN)
    {
        LOG_ERROR(MODULE_NAME, "Invalid length");
        return STATUS_ERR_INVALID_ARG;
    }

    uint8_t port = payload[GPIO_IRQ_UNBIND_PORT_IDX];
    uint8_t pin  = payload[GPIO_IRQ_UNBIND_PIN_IDX];

    if (port >= GPIO_NUM_OF_PORTS)
    {
        return STATUS_ERR_INVALID_PIN;
    }

    if (pin > MAX_PIN_COUNT)
    {
        return STATUS_ERR_INVALID_PIN;
    }

    /* Leaves the EXTI trigger armed - only the bound action is cleared.
     * Disarming the trigger itself is gpio_controller_irq_cfg()'s job
     * (EDGE = off). */
    if ((irq_bindings[pin].active == false) || (irq_bindings[pin].input_pin.port != (gpio_port_t)port))
    {
        LOG_ERROR(MODULE_NAME, "No active binding on this pin");
        return STATUS_ERR_INVALID_STATE;
    }

    irq_bindings[pin] = (irq_binding_t){0};

    return STATUS_OK;
}

status_t gpio_controller_io_cfg(const uint8_t *payload, uint8_t length)
{
    if (payload == NULL)
    {
        return STATUS_ERR_INVALID_ARG;
    }

    if (length != GPIO_CFG_PAYLOAD_LEN)
    {
        return STATUS_ERR_INVALID_ARG;
    }

    uint8_t dir  = payload[GPIO_CFG_DIR_IDX];
    uint8_t port = payload[GPIO_CFG_PORT_IDX];
    uint8_t pin  = payload[GPIO_CFG_PIN_IDX];

    if ((dir != (uint8_t)GPIO_MODE_INPUT) && (dir != (uint8_t)GPIO_MODE_OUTPUT))
    {
        return STATUS_ERR_INVALID_ARG;
    }

    if (port >= GPIO_NUM_OF_PORTS)
    {
        return STATUS_ERR_INVALID_PIN;
    }

    if (pin > MAX_PIN_COUNT)
    {
        return STATUS_ERR_INVALID_PIN;
    }

    const gpio_pin_t gpio = {
        .port = (gpio_port_t)port,
        .pin  = pin,
    };

    const gpio_config_t config = {
        .mode  = (gpio_mode_t)dir,
        .type  = GPIO_TYPE_PUSH_PULL,
        .speed = GPIO_SPEED_LOW,
        .pull  = GPIO_PULL_NONE,
    };

    return gpio_init(&gpio, &config);
}

status_t gpio_controller_write(const uint8_t *payload, uint8_t length)
{
    if (payload == NULL)
    {
        return STATUS_ERR_INVALID_ARG;
    }

    if (length != GPIO_WRITE_PAYLOAD_LEN)
    {
        return STATUS_ERR_INVALID_ARG;
    }

    uint8_t level = payload[GPIO_WRITE_LEVEL_IDX];
    uint8_t port  = payload[GPIO_WRITE_PORT_IDX];
    uint8_t pin   = payload[GPIO_WRITE_PIN_IDX];

    if ((level != (uint8_t)GPIO_LOW) && (level != (uint8_t)GPIO_HIGH))
    {
        return STATUS_ERR_INVALID_ARG;
    }

    if (port >= GPIO_NUM_OF_PORTS)
    {
        return STATUS_ERR_INVALID_PIN;
    }

    if (pin > MAX_PIN_COUNT)
    {
        return STATUS_ERR_INVALID_PIN;
    }

    const gpio_pin_t gpio = {
        .port = (gpio_port_t)port,
        .pin  = pin,
    };

    return gpio_set_state(&gpio, (gpio_state_t)level);
}

status_t gpio_controller_read(const uint8_t *payload, uint8_t length, bool *state)
{
    if ((payload == NULL) || (state == NULL))
    {
        return STATUS_ERR_INVALID_ARG;
    }

    if (length != GPIO_READ_PAYLOAD_LEN)
    {
        return STATUS_ERR_INVALID_ARG;
    }

    uint8_t port = payload[GPIO_READ_PORT_IDX];
    uint8_t pin  = payload[GPIO_READ_PIN_IDX];

    if (port >= GPIO_NUM_OF_PORTS)
    {
        return STATUS_ERR_INVALID_PIN;
    }

    if (pin > MAX_PIN_COUNT)
    {
        return STATUS_ERR_INVALID_PIN;
    }

    const gpio_pin_t gpio = {
        .port = (gpio_port_t)port,
        .pin  = pin,
    };

    return gpio_read(&gpio, state);
}

status_t gpio_controller_irq(const uint8_t *payload, uint8_t length)
{
    (void)payload;
    (void)length;

    return STATUS_ERR_UNSUPPORTED;
}
