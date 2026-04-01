#include "gpio.h"
#include "uart.h"

void delay(uint64_t ticks);

int main(void)
{
    const gpio_pin_t led       = {.port = GPIOA, .pin = (uint8_t)5U};
    const gpio_config_t config = {
        .mode  = GPIO_MODE_OUTPUT,
        .type  = GPIO_TYPE_PUSH_PULL,
        .speed = GPIO_SPEED_LOW,
        .pull  = GPIO_PULL_NONE,
    };
    int ret = 0;

    ret = gpio_init(&led, &config);

    while (ret == 0) {
        ret = gpio_toggle(&led);
        if (ret == 0) {
            delay(80000U);
        }
    }

    return ret;
}

void delay(uint64_t ticks)
{
    for (volatile uint64_t i = 0U; i < ticks; i++) {
    }
}
