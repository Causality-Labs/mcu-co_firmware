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

static GPIO_TypeDef *const gpio_ports[GPIO_NUM_OF_PORTS] = {
    GPIOA, GPIOB, GPIOC, GPIOD, GPIOE, GPIOF, GPIOG,
};

static gpio_irq_callback_t irq_callbacks[MAX_GPIO_INTERRUPTS] = {0};

static GPIO_TypeDef *get_port(const gpio_pin_t *gpio)
{
    return gpio_ports[gpio->port];
}

static bool is_valid_port(gpio_port_t port)
{
    return (uint32_t)port < GPIO_NUM_OF_PORTS;
}

static bool check_reserved_pins(const gpio_pin_t *gpio)
{
    if ((gpio->port == GPIO_PORT_A) && ((GPIOA_RESERVED_PINS & (1U << gpio->pin)) != 0U)) {
        return false;
    }

    if ((gpio->port == GPIO_PORT_B) && ((GPIOB_RESERVED_PINS & (1U << gpio->pin)) != 0U)) {
        return false;
    }

    return true;
}

static bool is_valid_pin(const gpio_pin_t *gpio)
{
    if (gpio == NULL) {
        return false;
    }

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

bool is_pin_an_input(const gpio_pin_t *gpio)
{
    return ((get_port(gpio)->MODER >> (gpio->pin * 2U)) & 0x3U) == 0x0U;
}

bool is_pin_an_output(const gpio_pin_t *gpio)
{
    return ((get_port(gpio)->MODER >> (gpio->pin * 2U)) & 0x3U) == 0x1U;
}

bool is_pin_an_af(const gpio_pin_t *gpio)
{
    return ((get_port(gpio)->MODER >> (gpio->pin * 2U)) & 0x3U) == 0x2U;
}

static IRQn_Type get_exti_irqn(uint8_t pin)
{
    if (pin <= 4U) {
        return (IRQn_Type)(EXTI0_IRQn + pin);
    } else if (pin <= 9U) {
        return EXTI9_5_IRQn;
    } else {
        return EXTI15_10_IRQn;
    }
}

int gpio_init(const gpio_pin_t *gpio, const gpio_config_t *config)
{
    if (config == NULL) {
        return -1;
    }

    if (!is_valid_pin(gpio)) {
        return -1;
    }

    GPIO_TypeDef *port = get_port(gpio);

    RCC->AHB2ENR |= gpio_clk_bits[(uint32_t)gpio->port];

    port->MODER &= ~(0x3U << (gpio->pin * 2U));
    port->MODER |= ((uint32_t)config->mode << (gpio->pin * 2U));

    port->OTYPER &= ~(0x1U << gpio->pin);
    port->OTYPER |= ((uint32_t)config->type << gpio->pin);

    port->OSPEEDR &= ~(0x3U << (gpio->pin * 2U));
    port->OSPEEDR |= ((uint32_t)config->speed << (gpio->pin * 2U));

    port->PUPDR &= ~(0x3U << (gpio->pin * 2U));
    port->PUPDR |= ((uint32_t)config->pull << (gpio->pin * 2U));

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
    get_port(gpio)->BSRR = (1U << gpio->pin);

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

    /* Atomic reset */
    get_port(gpio)->BRR = (1U << gpio->pin);

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

    get_port(gpio)->ODR ^= (0x1U << gpio->pin);

    return 0;
}

int gpio_read(const gpio_pin_t *gpio, bool *state)
{
    if (state == NULL) {
        return -1;
    }

    if (!is_valid_pin(gpio)) {
        return -1;
    }

    if (!is_pin_an_input(gpio)) {
        return -1;
    }

    if ((get_port(gpio)->IDR & (0x1U << gpio->pin)) != 0U) {
        *state = true;
    } else {
        *state = false;
    }

    return 0;
}

int gpio_init_interrupt(const gpio_pin_t *gpio, const gpio_irq_config_t *config)
{
    if (config == NULL) {
        return -1;
    }

    if (!is_valid_pin(gpio)) {
        return -1;
    }

    if (config->priority > FMAC_IRQn) {
        return -1;
    }

    uint8_t exticr_idx   = gpio->pin / 4U;
    uint8_t exticr_shift = (gpio->pin % 4U) * 4U;
    uint32_t port_idx    = (uint32_t)gpio->port;

    /*Clock Enable here.*/
    RCC->APB2ENR |= RCC_APB2ENR_SYSCFGEN;

    SYSCFG->EXTICR[exticr_idx] &= ~(0xFU << exticr_shift);
    SYSCFG->EXTICR[exticr_idx] |= (port_idx << exticr_shift);

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

int gpio_set_af(const gpio_pin_t *gpio, gpio_af_t af)
{
    if (!is_valid_pin(gpio)) {
        return -1;
    }

    GPIO_TypeDef *port = get_port(gpio);

    /* AFR[0] = AFRL (pins 0-7), AFR[1] = AFRH (pins 8-15).
     * Each pin occupies a 4-bit field. */
    if (gpio->pin < 8U) {
        port->AFR[0] &= ~(0xFUL << (gpio->pin * 4U));
        port->AFR[0] |= ((uint32_t)af << (gpio->pin * 4U));
    } else {
        port->AFR[1] &= ~(0xFUL << ((gpio->pin - 8U) * 4U));
        port->AFR[1] |= ((uint32_t)af << ((gpio->pin - 8U) * 4U));
    }

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
