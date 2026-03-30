#include <stdio.h>
#include "gpio.h"
#include "uart.h"

int main (void)
{
    int ret = gpio_init();
    if (ret != 0)
        return -1;

    while (1)
    {

    }

    return 0;
}