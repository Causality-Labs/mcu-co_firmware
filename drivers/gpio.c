#include "gpio.h"

static const uint32_t gpio_clk_bits[] = {
    RCC_AHB2ENR_GPIOAEN, RCC_AHB2ENR_GPIOBEN, RCC_AHB2ENR_GPIOCEN, RCC_AHB2ENR_GPIODEN,
    RCC_AHB2ENR_GPIOEEN, RCC_AHB2ENR_GPIOFEN, RCC_AHB2ENR_GPIOGEN,
};

static bool check_reserved_pins(const gpio_pin_t *gpio)
{
    if ((gpio->port == GPIOA) && ((GPIOA_RESERVED_PINS & (1U << gpio->pin)) != 0U)) {
        return false;
    }

    if ((gpio->port == GPIOB) && ((GPIOB_RESERVED_PINS & (1U << gpio->pin)) != 0U)) {
        return false;
    }

    return true;
}

static bool is_valid_port(const GPIO_TypeDef *port)
{
    return (port == GPIOA) || (port == GPIOB) || (port == GPIOC) || (port == GPIOD) ||
           (port == GPIOE) || (port == GPIOF) || (port == GPIOG);
}

static bool is_valid_pin(const gpio_pin_t *gpio)
{
    if (!is_valid_port(gpio->port)) {
        return false;
    }

    if (!check_reserved_pins(gpio)) {
        return false;
    }

    if (gpio->pin > MAX_PIN_COUNT) {
        return false;
    }

    return true;
}

static bool is_pin_an_output(const gpio_pin_t *gpio)
{
    return ((gpio->port->MODER >> (gpio->pin * 2U)) & 0x3U) == 0x1U;
}

static bool is_pin_an_input(const gpio_pin_t *gpio)
{
    return ((gpio->port->MODER >> (gpio->pin * 2U)) & 0x3U) == 0x0U;
}

int gpio_init(const gpio_pin_t *gpio, const gpio_config_t *config)
{
    if (!is_valid_pin(gpio)) {
        return -1;
    }

    uint32_t idx = ((uint32_t)gpio->port - (uint32_t)GPIOA) / sizeof(GPIO_TypeDef);
    if (idx >= (sizeof(gpio_clk_bits) / sizeof(gpio_clk_bits[0]))) {
        return -1;
    }
    RCC->AHB2ENR |= gpio_clk_bits[idx];

    gpio->port->MODER &= ~(0x3U << (gpio->pin * 2U));
    gpio->port->MODER |= ((uint32_t)config->mode << (gpio->pin * 2U));

    gpio->port->OTYPER &= ~(0x1U << gpio->pin);
    gpio->port->OTYPER |= ((uint32_t)config->type << gpio->pin);

    gpio->port->OSPEEDR &= ~(0x3U << (gpio->pin * 2U));
    gpio->port->OSPEEDR |= ((uint32_t)config->speed << (gpio->pin * 2U));

    gpio->port->PUPDR &= ~(0x3U << (gpio->pin * 2U));
    gpio->port->PUPDR |= ((uint32_t)config->pull << (gpio->pin * 2U));

    return 0;
}

int gpio_set(const gpio_pin_t *gpio)
{
    if (!is_valid_pin(gpio)) {
        return -1;
    }

    if (!is_pin_an_output(gpio)) {
        return -1;
    }

    gpio->port->BSRR = (1U << gpio->pin);

    return 0;
}

int gpio_reset(const gpio_pin_t *gpio)
{
    if (!is_valid_pin(gpio)) {
        return -1;
    }

    if (!is_pin_an_output(gpio)) {
        return -1;
    }

    gpio->port->BRR = (1U << gpio->pin);

    return 0;
}

int gpio_set_state(const gpio_pin_t *gpio, bool state)
{
    return state ? gpio_set(gpio) : gpio_reset(gpio);
}

int gpio_toggle(const gpio_pin_t *gpio)
{
    if (!is_valid_pin(gpio)) {
        return -1;
    }
    if (!is_pin_an_output(gpio)) {
        return -1;
    }
    gpio->port->ODR ^= (0x1U << gpio->pin);

    return 0;
}

int gpio_read(const gpio_pin_t *gpio, bool *state)
{
    if (!is_valid_pin(gpio)) {
        return -1;
    }

    if (!is_pin_an_input(gpio)) {
        return -1;
    }

    if ((gpio->port->IDR & (0x1U << gpio->pin)) != 0U) {
        *state = true;
    } else {
        *state = false;
    }

    return 0;
}
