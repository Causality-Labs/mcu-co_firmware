#include <stddef.h>
#include "gpio.h"

void EXTI0_IRQHandler(void);
void EXTI1_IRQHandler(void);
void EXTI2_IRQHandler(void);
void EXTI3_IRQHandler(void);
void EXTI4_IRQHandler(void);
void EXTI9_5_IRQHandler(void);
void EXTI15_10_IRQHandler(void);

static const uint32_t gpio_clk_bits[GPIO_NUM_OF_PORTS] = {
    RCC_AHB2ENR_GPIOAEN, RCC_AHB2ENR_GPIOBEN, RCC_AHB2ENR_GPIOCEN, RCC_AHB2ENR_GPIODEN,
    RCC_AHB2ENR_GPIOEEN, RCC_AHB2ENR_GPIOFEN, RCC_AHB2ENR_GPIOGEN,
};

static gpio_irq_callback_t irq_callbacks[MAX_GPIO_INTERRUPTS] = {0};

extern const gpio_pin_t led;

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

static IRQn_Type get_exti_irqn(uint8_t pin)
{
    if (pin <= 4U) {
        return (IRQn_Type)(EXTI0_IRQn + pin); // EXTI0-4 are sequential
    } else if (pin <= 9U) {
        return EXTI9_5_IRQn;
    } else {
        return EXTI15_10_IRQn;
    }
}

int gpio_init(const gpio_pin_t *gpio, const gpio_config_t *config)
{
    if (!is_valid_pin(gpio)) {
        return -1;
    }

    uint32_t idx;
    if (gpio->port == GPIOA) {
        idx = 0U;
    } else if (gpio->port == GPIOB) {
        idx = 1U;
    } else if (gpio->port == GPIOC) {
        idx = 2U;
    } else if (gpio->port == GPIOD) {
        idx = 3U;
    } else if (gpio->port == GPIOE) {
        idx = 4U;
    } else if (gpio->port == GPIOF) {
        idx = 5U;
    } else if (gpio->port == GPIOG) {
        idx = 6U;
    } else {
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

    /* Atomic set */
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

    /* Atomic set */
    gpio->port->BRR = (1U << gpio->pin);

    return 0;
}

int gpio_set_state(const gpio_pin_t *gpio, gpio_state_t state)
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

int gpio_init_interrupt(const gpio_pin_t *gpio, const gpio_irq_config_t *config)
{
    if (!is_valid_pin(gpio)) {
        return -1;
    }

    if (config->priority > FMAC_IRQn) {
        return -1;
    }

    /*Clock Enable here.*/
    RCC->APB2ENR |= RCC_APB2ENR_SYSCFGEN;

    /* EXTICR config here */
    uint8_t exticr_idx   = gpio->pin / 4U;
    uint8_t exticr_shift = (gpio->pin % 4U) * 4U;
    uint32_t port_idx;

    if (gpio->port == GPIOA) {
        port_idx = 0U;
    } else if (gpio->port == GPIOB) {
        port_idx = 1U;
    } else if (gpio->port == GPIOC) {
        port_idx = 2U;
    } else if (gpio->port == GPIOD) {
        port_idx = 3U;
    } else if (gpio->port == GPIOE) {
        port_idx = 4U;
    } else if (gpio->port == GPIOF) {
        port_idx = 5U;
    } else if (gpio->port == GPIOG) {
        port_idx = 6U;
    } else {
        return -1;
    }

    SYSCFG->EXTICR[exticr_idx] &= ~(0xFU << exticr_shift);    // clear the 4-bit field
    SYSCFG->EXTICR[exticr_idx] |= (port_idx << exticr_shift); // write port selection

    /* Set IMR1 */
    EXTI->IMR1 |= (0x1U << gpio->pin);

    /* Set RTSR1/FTSR1 */
    EXTI->RTSR1 &= ~(1U << gpio->pin);
    EXTI->FTSR1 &= ~(1U << gpio->pin);

    if (config->trigger == RISING) {
        EXTI->RTSR1 |= (0x1U << gpio->pin);
    } else if (config->trigger == FALLING) {
        EXTI->FTSR1 |= (0x1U << gpio->pin);
    } else {
        EXTI->RTSR1 |= (0x1U << gpio->pin);
        EXTI->FTSR1 |= (0x1U << gpio->pin);
    }

    irq_callbacks[gpio->pin] = config->callback;

    IRQn_Type interrupt_number = get_exti_irqn(gpio->pin);
    NVIC_SetPriority(interrupt_number, config->priority);
    NVIC_EnableIRQ(interrupt_number);

    return 0;
}

int gpio_deinit_interrupt(const gpio_pin_t *gpio)
{
    if (!is_valid_pin(gpio)) {
        return -1;
    }

    IRQn_Type interrupt_number = get_exti_irqn(gpio->pin);

    NVIC_DisableIRQ(interrupt_number);

    EXTI->IMR1 &= ~(1U << gpio->pin);
    EXTI->RTSR1 &= ~(1U << gpio->pin);
    EXTI->FTSR1 &= ~(1U << gpio->pin);

    uint32_t exticr_idx   = gpio->pin / 4U;
    uint32_t exticr_shift = (gpio->pin % 4U) * 4U;
    SYSCFG->EXTICR[exticr_idx] &= ~(0xFU << exticr_shift);

    irq_callbacks[gpio->pin] = NULL;

    return 0;
}

/* Check if an interrupt is pending,
run it and then clear the interrupt.*/
void EXTI0_IRQHandler(void)
{
    if (!(EXTI->PR1 & ((1U << 0)))) {
        return;
    }

    EXTI->PR1 |= (1U << 0);

    if (irq_callbacks[0] == NULL) {
        return;
    }

    irq_callbacks[0]();

    return;
}

void EXTI1_IRQHandler(void)
{
    if (!(EXTI->PR1 & ((1U << 1)))) {
        return;
    }

    EXTI->PR1 |= (1U << 1);

    if (irq_callbacks[1] == NULL) {
        return;
    }

    irq_callbacks[1]();

    return;
}

void EXTI2_IRQHandler(void)
{
    if (!(EXTI->PR1 & (1U << 2U))) {
        return;
    }

    EXTI->PR1 |= (1U << 2U);

    if (irq_callbacks[2] == NULL) {
        return;
    }

    irq_callbacks[2]();
}

void EXTI3_IRQHandler(void)
{
    if (!(EXTI->PR1 & (1U << 3U))) {
        return;
    }

    EXTI->PR1 |= (1U << 3U);

    if (irq_callbacks[3] == NULL) {
        return;
    }

    irq_callbacks[3]();
}

void EXTI4_IRQHandler(void)
{
    if (!(EXTI->PR1 & (1U << 4U))) {
        return;
    }

    EXTI->PR1 |= (1U << 4U);

    if (irq_callbacks[4] == NULL) {
        return;
    }

    irq_callbacks[4]();
}

void EXTI9_5_IRQHandler(void)
{
    for (uint8_t pin = 5U; pin <= 9U; pin++) {
        if (!(EXTI->PR1 & (1U << pin))) {
            continue;
        }

        EXTI->PR1 |= (1U << pin);

        if (irq_callbacks[pin] == NULL) {
            continue;
        }

        irq_callbacks[pin]();
    }

    return;
}

void EXTI15_10_IRQHandler(void)
{
    for (uint8_t pin = 10U; pin <= 15U; pin++) {
        if (!(EXTI->PR1 & (1U << pin))) {
            continue;
        }

        EXTI->PR1 |= (1U << pin);

        if (irq_callbacks[pin] == NULL) {
            continue;
        }

        irq_callbacks[pin]();
    }

    return;
}