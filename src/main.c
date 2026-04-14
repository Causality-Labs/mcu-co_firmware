#include "gpio.h"
#include "uart.h"

const gpio_pin_t led      = {.port = GPIO_PORT_A, .pin = (uint8_t)5U};
const gpio_pin_t button   = {.port = GPIO_PORT_C, .pin = (uint8_t)13U};
const uart_handle_t debug = {.instance = UART_INSTANCE_USART1};

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

    ret = uart_init(&debug, &debug_config);
    if (ret != 0) {
        for (;;) {
        }
    }

    const uint8_t msg[] = "Hello, World!\r\n";
    ret                 = uart_write_buffer(&debug, msg, (uint16_t)(sizeof(msg) - 1U));
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

    for (;;) {
        delay(1000000U);
        ret = uart_write_buffer(&debug, msg, (uint16_t)(sizeof(msg) - 1U));
        if (ret != 0) {
            for (;;) {
            }
        }
        (void)gpio_toggle(&led);
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