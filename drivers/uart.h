#ifndef UART_H
#define UART_H

#include <stdint.h>
#include "stm32g474xx.h"
#include "gpio.h"

#define NUM_OF_UART_PORTS 6

typedef enum {
    UART_BAUD_9600   = 9600U,
    UART_BAUD_19200  = 19200U,
    UART_BAUD_38400  = 38400U,
    UART_BAUD_57600  = 57600U,
    UART_BAUD_115200 = 115200U,
} uart_baudrate_t;

typedef enum {
    UART_PARITY_NONE = 0U,
    UART_PARITY_EVEN = 1U,
    UART_PARITY_ODD  = 2U,
} uart_parity_t;

typedef enum {
    UART_DATA_8BIT = 0U,
    UART_DATA_9BIT = 1U,
    UART_DATA_7BIT = 2U,
} uart_data_width_t;

typedef enum {
    UART_STOP_1BIT = 0U,
    UART_STOP_2BIT = 1U,
} uart_stop_bits_t;

typedef enum {
    UART_MODE_TX    = 0U,
    UART_MODE_RX    = 1U,
    UART_MODE_TX_RX = 2U,
} uart_mode_t;

typedef struct {
    volatile uint32_t *reg;
    uint32_t bit;
} uart_clk_t;

typedef enum {
    UART_INSTANCE_USART1  = 0U,
    UART_INSTANCE_USART2  = 1U,
    UART_INSTANCE_USART3  = 2U,
    UART_INSTANCE_UART4   = 3U,
    UART_INSTANCE_UART5   = 4U,
    UART_INSTANCE_LPUART1 = 5U,
} uart_instance_t;

typedef struct {
    uart_baudrate_t baudrate;
    uart_parity_t parity;
    uart_data_width_t data_width;
    uart_stop_bits_t stop_bits;
    uart_mode_t mode;
} uart_config_t;

typedef struct {
    uart_instance_t instance;
} uart_handle_t;

typedef struct {
    gpio_pin_t tx;
    gpio_pin_t rx;
    gpio_af_t af;
} uart_pins_t;

typedef struct {
    uint8_t  *buffer;
    uint16_t  size;
} uart_rx_buffer_t;

int uart_init(const uart_handle_t *handle, const uart_config_t *config, const uart_rx_buffer_t *rx_buffer);
int uart_deinit(const uart_handle_t *handle);
int uart_write_buffer(const uart_handle_t *handle, const uint8_t *data, uint16_t length);
int uart_read_buffer(const uart_handle_t *handle, uint8_t *data, uint16_t length);

#endif /* UART_H */
