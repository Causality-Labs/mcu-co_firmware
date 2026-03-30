#include "gpio.h"

#define GPIOA_RESERVED_PINS  ((1U << 13) | (1U << 14) | (1U << 15))
#define GPIOB_RESERVED_PINS  ((1U << 3)  | (1U << 4))

int gpio_init(GPIO_TypeDef *port, uint8_t pin, gpio_config_t *config)
{

    /* make sure user can not access reserved pins. */
    if ((port == GPIOA) && (GPIOA_RESERVED_PINS & (1U << pin)))
        return -1;

    if ((port == GPIOB) && (GPIOB_RESERVED_PINS & (1U << pin)))
        return -1;

    /* enable GPIOA peripheral clock */
    RCC->AHB2ENR |= RCC_AHB2ENR_GPIOAEN;

    /* pin mode: input / output / alternate function / analog (2 bits per pin) */
    port->MODER &= ~(0x3 << (pin * 2));
    port->MODER |= (config->mode << (pin * 2));

    /* output type: push-pull or open-drain (1 bit per pin) */
    port->OTYPER &= ~(0x3 << (pin * 2));
    port->OTYPER |= (config->type << (pin * 2));

    /* Speed */
    port->OSPEEDR &= ~(0x3 << (pin * 2));
    port->OSPEEDR |= ~(config->speed << (pin * 2));

    /* pull-up / pull-down / none (2 bits per pin) */
    port->PUPDR &= ~(0x3 << (pin * 2));
    port->PUPDR |= (config->pull << (pin * 2));

    return 0;
}

int gpio_set(GPIO_TypeDef *port, uint8_t pin)
{
    /* make sure user can not access reserved pins. */
    if ((port == GPIOA) && (GPIOA_RESERVED_PINS & (1U << pin)))
        return -1;

    if ((port == GPIOB) && (GPIOB_RESERVED_PINS & (1U << pin)))
        return -1;

    return 0;
}