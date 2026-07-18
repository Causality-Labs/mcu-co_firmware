#include <stddef.h>
#include <stdint.h>
#include "gpio.h"
#include "gpio_controller.h"
#include "status.h"

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

status_t gpio_controller_cfg(const uint8_t *payload, uint8_t length)
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
