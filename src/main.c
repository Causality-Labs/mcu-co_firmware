#include <stdio.h>
#include "gpio.h"
#include "uart.h"

const gpio_pin_t led        = {.port = GPIO_PORT_A, .pin = (uint8_t)5U};
const gpio_pin_t button     = {.port = GPIO_PORT_C, .pin = (uint8_t)13U};
const uart_instance_t debug = UART_INSTANCE_USART1;

static uint8_t rx_storage[64];

void delay(uint64_t ticks);
void button_ISR(void);

int main(void)
{
    int ret = 0;

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

    const uart_config_t debug_config = {
        .baudrate   = UART_BAUD_115200,
        .data_width = UART_DATA_8BIT,
        .parity     = UART_PARITY_NONE,
        .stop_bits  = UART_STOP_1BIT,
        .mode       = UART_MODE_TX_RX,
    };

    const uart_rx_buffer_t rx_buf = {
        .buffer = rx_storage,
        .size   = sizeof(rx_storage),
    };

    ret = uart_init(debug, &debug_config, &rx_buf);
    if (ret != 0) {
        for (;;) {
        }
    }

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

    uint8_t echo_buf[64];

    for (;;) {

        int n = uart_read_buffer(debug, echo_buf, (uint16_t)sizeof(echo_buf));
        if (n > 0) {
            (void)uart_write_buffer(debug, echo_buf, (uint16_t)n);
            (void)gpio_toggle(&led);
        }

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
    return;
}