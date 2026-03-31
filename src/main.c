#include <stdio.h>
#include "gpio.h"
#include "uart.h"

void delay(uint64_t ticks)
{
    for (volatile uint64_t i = 0; i < ticks; i++) {}

    return;
}

int main (void)
{
    gpio_config_t config = {
        .mode  = 1,  /* output */
        .type  = 0,  /* push-pull */
        .speed = 0,  /* low speed */
        .pull  = 0,  /* no pull */
    };

    int ret = gpio_init(GPIOA, 5, &config);
    if (ret != 0)
        return -1;

    while (1)
    {
        if (gpio_toggle(GPIOA, 5))
            return -1;

        delay(8000000);
    }

    return 0;
}