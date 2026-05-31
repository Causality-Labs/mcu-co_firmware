#include <stdio.h>
#include "gpio.h"
#include "uart.h"
#include "rcc.h"
#include "logger.h"

#define MODULE_NAME "MAIN"

const gpio_pin_t led    = {.port = GPIO_PORT_A, .pin = (uint8_t)5U};
const gpio_pin_t button = {.port = GPIO_PORT_C, .pin = (uint8_t)13U};

static volatile uint8_t button_event = 0U;

void delay(uint64_t ticks);
void button_ISR(void);

int main(void)
{
    int ret = 0;

    ret = rcc_init(RCC_SYSCLK_HSI_170MHZ);
    if (ret != 0) {
        for (;;) {
        }
    }

    ret = logger_init();

    if (ret != 0) {
        for (;;) {
        }
    }

    LOG_INFO(MODULE_NAME, "boot");
    LOG_DEBUG(MODULE_NAME, "logger initialised");

    const gpio_config_t button_config = {
        .mode  = GPIO_MODE_INPUT,
        .type  = GPIO_TYPE_PUSH_PULL,
        .speed = GPIO_SPEED_LOW,
        .pull  = GPIO_PULL_NONE,
    };

    const gpio_config_t led_config = {
        .mode  = GPIO_MODE_OUTPUT,
        .type  = GPIO_TYPE_PUSH_PULL,
        .speed = GPIO_SPEED_LOW,
        .pull  = GPIO_PULL_NONE,
    };

    ret = gpio_init(&led, &led_config);
    if (ret != 0) {
        LOG_ERROR(MODULE_NAME, "LED gpio_init failed");
        for (;;) {
        }
    }
    LOG_DEBUG(MODULE_NAME, "LED configured");

    ret = gpio_init(&button, &button_config);
    if (ret != 0) {
        LOG_ERROR(MODULE_NAME, "button gpio_init failed");
        for (;;) {
        }
    }
    LOG_DEBUG(MODULE_NAME, "button configured");

    gpio_irq_config_t irq_cfg = {
        .trigger  = RISING,
        .callback = button_ISR,
        .priority = 5,
    };

    ret = gpio_init_interrupt(&button, &irq_cfg);
    if (ret != 0) {
        LOG_ERROR(MODULE_NAME, "button IRQ init failed");
        for (;;) {
        }
    }
    LOG_INFO(MODULE_NAME, "button IRQ armed");

    LOG_INFO(MODULE_NAME, "entering main loop");

    for (;;) {
        if (button_event != 0U) {
            button_event = 0U;
            LOG_INFO(MODULE_NAME, "button pressed");
        }
        logger_flush();
        delay(10000U);
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
    button_event = 1U;
}