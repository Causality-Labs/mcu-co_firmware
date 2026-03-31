#include "gpio.h"

static const uint32_t gpio_clk_bits[] = {
    RCC_AHB2ENR_GPIOAEN,
    RCC_AHB2ENR_GPIOBEN,
    RCC_AHB2ENR_GPIOCEN,
    RCC_AHB2ENR_GPIODEN,
    RCC_AHB2ENR_GPIOEEN,
    RCC_AHB2ENR_GPIOFEN,
    RCC_AHB2ENR_GPIOGEN,
};

static bool check_reserved_pins(GPIO_TypeDef *port, uint8_t pin)
{
    /* make sure user can not access reserved pins. */
    if ((port == GPIOA) && (GPIOA_RESERVED_PINS & (1U << pin)))
        return false;

    if ((port == GPIOB) && (GPIOB_RESERVED_PINS & (1U << pin)))
        return false;

    return true;
}

static bool is_valid_pin(GPIO_TypeDef *port, uint8_t pin)
{
    if (!check_reserved_pins(port, pin))
        return false;

    if (pin > MAX_PIN_COUNT)
        return false;

    return true;
}

static bool is_pin_an_output(GPIO_TypeDef *port, uint8_t pin)
{
    return ((port->MODER >> (pin * 2)) & 0x3U) == 0x1U;
}

static bool is_pin_an_input(GPIO_TypeDef *port, uint8_t pin)
{
    return ((port->MODER >> (pin * 2)) & 0x3U) == 0x0U;  
}

int gpio_init(GPIO_TypeDef *port, uint8_t pin, gpio_config_t *config)
{
    if (!is_valid_pin(port, pin))
        return -1;

    /* enable port clock */
    uint32_t idx = ((uint32_t)port - (uint32_t)GPIOA) / sizeof(GPIO_TypeDef);
    RCC->AHB2ENR |= gpio_clk_bits[idx];

    /* pin mode: input / output / alternate function / analog (2 bits per pin) */
    port->MODER &= ~(0x3U << (pin * 2));
    port->MODER |=  ((uint32_t)config->mode  << (pin * 2));

    /* output type: push-pull or open-drain (1 bit per pin) */
    port->OTYPER &= ~(0x1U << pin);
    port->OTYPER |=  ((uint32_t)config->type << pin);

    /* Speed */
    port->OSPEEDR &= ~(0x3U << (pin * 2));
    port->OSPEEDR |=  ((uint32_t)config->speed << (pin * 2));

    /* pull-up / pull-down / none (2 bits per pin) */
    port->PUPDR &= ~(0x3U << (pin * 2));
    port->PUPDR |=  ((uint32_t)config->pull  << (pin * 2));

    return 0;
}

int gpio_set(GPIO_TypeDef *port, uint8_t pin)
{
    /* make sure user can not access reserved pins. */
    if (!is_valid_pin(port, pin))
        return -1;

    /* Is pin configured as an output.*/
    if(!is_pin_an_output(port, pin))
        return -1;

    /* Check if pin is configures as a gpio output */
    port->BSRR = (1U << pin); 

    return 0;
}

int gpio_reset(GPIO_TypeDef *port, uint8_t pin)
{
    if (!is_valid_pin(port, pin))
        return -1;

    /* Is pin configured as an output.*/
    if(!is_pin_an_output(port, pin))
        return -1;

    port->BRR = (1U << pin);

    return 0;
}

int gpio_set_state(GPIO_TypeDef *port, uint8_t pin, bool state)
{
    return state ? gpio_set(port, pin) : gpio_reset(port, pin);
}

int gpio_toggle(GPIO_TypeDef *port, uint8_t pin)
{
    if (!is_valid_pin(port, pin))
        return -1;

    /* Is pin configured as an output.*/
    if(!is_pin_an_output(port, pin))
        return -1;

    port->ODR ^= (0x1U << pin);

    return 0;
}
int gpio_read(GPIO_TypeDef *port, uint8_t pin, bool *state)
{
    if (!is_valid_pin(port, pin))
        return -1;

    /* Is pin configured as an output.*/
    if(!is_pin_an_input(port, pin))
        return -1;

    if (port->IDR & (0x1U << pin))
        *state = true;
    else
        *state = false;

    return 0;
}