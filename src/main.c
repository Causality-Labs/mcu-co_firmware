#include "gpio.h"
#include "uart.h"

const gpio_pin_t led           = {.port = GPIOA, .pin = (uint8_t)5U};
const gpio_config_t led_config = {
    .mode  = GPIO_MODE_OUTPUT,
    .type  = GPIO_TYPE_PUSH_PULL,
    .speed = GPIO_SPEED_LOW,
    .pull  = GPIO_PULL_NONE,
};

const gpio_pin_t button           = {.port = GPIOC, .pin = (uint8_t)13U};
const gpio_config_t button_config = {
    .mode  = GPIO_MODE_INPUT,
    .type  = GPIO_TYPE_PUSH_PULL,
    .speed = GPIO_SPEED_LOW,
    .pull  = GPIO_PULL_NONE,
};

void delay(uint64_t ticks);
void button_ISR(void);

int main(void)
{
    int ret = 0;

    ret = gpio_init(&led, &led_config);
    if (ret != 0) {
        for (;;) {
        }
    }

    ret = gpio_init(&button, &button_config);
    if (ret != 0) {
        for (;;) {
        }
    }

    gpio_irq_config_t irq_cfg = {
        .trigger  = RISING,
        .callback = button_ISR,
        .priority = 5,
    };

    ret = gpio_init_interrupt(&button, &irq_cfg);
    if (ret != 0) {
        for (;;) {
        }
    }

    for (;;) {
    }

    return 0;
}

void delay(uint64_t ticks)
{
    for (volatile uint64_t i = 0U; i < ticks; i++) {
    }
}

void button_ISR(void)
{
    (void)gpio_toggle(&led);
    return;
}