#include <stddef.h>
#include <stdbool.h>
#include "uart.h"
#include "gpio.h"

/* HSI at 16 MHz, APB1 and APB2 prescalers = 1 (reset defaults) */
#define UART_CLOCK_HZ      16000000U
#define LPUART_CLOCK_HZ    16000000U
#define UART_READY_TIMEOUT 1000U

static USART_TypeDef *const uart_channels[NUM_OF_UART_PORTS] = {
    USART1, USART2, USART3, UART4, UART5, LPUART1,
};

static const uart_clk_t uart_clk[NUM_OF_UART_PORTS] = {
    [UART_INSTANCE_USART1]  = {.reg = &RCC->APB2ENR, .bit = RCC_APB2ENR_USART1EN},
    [UART_INSTANCE_USART2]  = {.reg = &RCC->APB1ENR1, .bit = RCC_APB1ENR1_USART2EN},
    [UART_INSTANCE_USART3]  = {.reg = &RCC->APB1ENR1, .bit = RCC_APB1ENR1_USART3EN},
    [UART_INSTANCE_UART4]   = {.reg = &RCC->APB1ENR1, .bit = RCC_APB1ENR1_UART4EN},
    [UART_INSTANCE_UART5]   = {.reg = &RCC->APB1ENR1, .bit = RCC_APB1ENR1_UART5EN},
    [UART_INSTANCE_LPUART1] = {.reg = &RCC->APB1ENR2, .bit = RCC_APB1ENR2_LPUART1EN},
};

static const uart_pins_t uart_pins[NUM_OF_UART_PORTS] = {
    [UART_INSTANCE_USART1]  = {.tx = {GPIO_PORT_A, 9U}, .rx = {GPIO_PORT_A, 10U}, .af = GPIO_AF7},
    [UART_INSTANCE_USART2]  = {.tx = {GPIO_PORT_A, 2U}, .rx = {GPIO_PORT_A, 3U}, .af = GPIO_AF7},
    [UART_INSTANCE_USART3]  = {.tx = {GPIO_PORT_B, 10U}, .rx = {GPIO_PORT_B, 11U}, .af = GPIO_AF7},
    [UART_INSTANCE_UART4]   = {.tx = {GPIO_PORT_C, 10U}, .rx = {GPIO_PORT_C, 11U}, .af = GPIO_AF8},
    [UART_INSTANCE_UART5]   = {.tx = {GPIO_PORT_C, 12U}, .rx = {GPIO_PORT_D, 2U}, .af = GPIO_AF8},
    [UART_INSTANCE_LPUART1] = {.tx = {GPIO_PORT_A, 2U}, .rx = {GPIO_PORT_A, 3U}, .af = GPIO_AF12},
};

static bool uart_initialized[NUM_OF_UART_PORTS] = {
    [UART_INSTANCE_USART1]  = false,
    [UART_INSTANCE_USART2]  = false,
    [UART_INSTANCE_USART3]  = false,
    [UART_INSTANCE_UART4]   = false,
    [UART_INSTANCE_UART5]   = false,
    [UART_INSTANCE_LPUART1] = false,
};

static bool is_gpio_pin_being_used(const uart_pins_t *pins)
{
    if (is_pin_an_input(&pins->rx) || is_pin_an_output(&pins->rx) || is_pin_an_af(&pins->rx)) {
        return true;
    }

    if (is_pin_an_input(&pins->tx) || is_pin_an_output(&pins->tx) || is_pin_an_af(&pins->tx)) {
        return true;
    }

    return false;
}

static void set_data_width(USART_TypeDef *uart_channel, const uart_config_t *config)
{
    uart_channel->CR1 &= ~(USART_CR1_M0 | USART_CR1_M1);
    if (config->data_width == UART_DATA_9BIT) {
        uart_channel->CR1 |= USART_CR1_M0;
    } else if (config->data_width == UART_DATA_7BIT) {
        uart_channel->CR1 |= USART_CR1_M1;
    } else {
        /* 8-bit: M1=0, M0=0 — already cleared above */
    }

    return;
}

static void set_parity_bits(USART_TypeDef *uart_channel, const uart_config_t *config)
{
    uart_channel->CR1 &= ~(USART_CR1_PCE | USART_CR1_PS);
    if (config->parity == UART_PARITY_EVEN) {
        uart_channel->CR1 |= USART_CR1_PCE;
    } else if (config->parity == UART_PARITY_ODD) {
        uart_channel->CR1 |= (USART_CR1_PCE | USART_CR1_PS);
    }
}

static void set_stop_bits(USART_TypeDef *uart_channel, const uart_config_t *config)
{
    /* STOP[1:0]: 00 = 1 stop bit, 10 = 2 stop bits */
    uart_channel->CR2 &= ~USART_CR2_STOP;
    if (config->stop_bits == UART_STOP_2BIT) {
        uart_channel->CR2 |= USART_CR2_STOP_1;
    }
}

static void set_baud_rate(USART_TypeDef *uart_channel, const uart_config_t *config, bool is_lpuart)
{
    /* LPUART BRR = (f_clk * 256) / baud, UART/USART BRR = f_clk / baud */
    if (is_lpuart) {
        uart_channel->BRR = (LPUART_CLOCK_HZ * 256U) / (uint32_t)config->baudrate;
    } else {
        uart_channel->BRR = UART_CLOCK_HZ / (uint32_t)config->baudrate;
    }
}

static int wait_for_ack(const USART_TypeDef *uart_channel, uint32_t flag)
{
    uint32_t retries = UART_READY_TIMEOUT;
    while ((uart_channel->ISR & flag) == 0U) {
        if (retries == 0U) {
            return -1;
        }
        retries--;
    }
    return 0;
}

static void set_mode(USART_TypeDef *uart_channel, const uart_config_t *config)
{
    uart_channel->CR1 &= ~(USART_CR1_TE | USART_CR1_RE);
    if (config->mode == UART_MODE_TX) {
        uart_channel->CR1 |= USART_CR1_TE;
    } else if (config->mode == UART_MODE_RX) {
        uart_channel->CR1 |= USART_CR1_RE;
    } else {
        uart_channel->CR1 |= (USART_CR1_TE | USART_CR1_RE);
    }
}

static int uart_gpio_config(const uart_pins_t *pins)
{
    gpio_config_t af_config = {
        .mode  = GPIO_MODE_AF,
        .type  = GPIO_TYPE_PUSH_PULL,
        .speed = GPIO_SPEED_HIGH,
        .pull  = GPIO_PULL_NONE,
    };

    int ret = gpio_init(&pins->tx, &af_config);
    if (ret != 0) {
        return -1;
    }
    ret = gpio_init(&pins->rx, &af_config);
    if (ret != 0) {
        return -1;
    }

    ret = gpio_set_af(&pins->tx, pins->af);
    if (ret != 0) {
        return -1;
    }

    ret = gpio_set_af(&pins->rx, pins->af);
    if (ret != 0) {
        return -1;
    }

    return 0;
}

int uart_init(const uart_handle_t *handle, const uart_config_t *config)
{
    if (handle == NULL || config == NULL) {
        return -1;
    }

    if (handle->instance >= NUM_OF_UART_PORTS) {
        return -1;
    }

    const uart_pins_t *pins = &uart_pins[handle->instance];
    if (is_gpio_pin_being_used(pins)) {
        return -1;
    }

    USART_TypeDef *uart_channel = uart_channels[handle->instance];
    const uart_clk_t *clk       = &uart_clk[handle->instance];

    *clk->reg |= clk->bit;
    uart_channel->CR1 &= ~USART_CR1_UE;

    if (uart_gpio_config(pins) != 0) {
        return -1;
    }

    set_data_width(uart_channel, config);
    set_parity_bits(uart_channel, config);
    set_stop_bits(uart_channel, config);
    bool is_lp_uart = (handle->instance == UART_INSTANCE_LPUART1);
    set_baud_rate(uart_channel, config, is_lp_uart);
    set_mode(uart_channel, config);

    uart_channel->CR1 |= USART_CR1_UE;

    if (config->mode == UART_MODE_TX || config->mode == UART_MODE_TX_RX) {
        // if (wait_for_ack(uart_channel, USART_ISR_TEACK) != 0) {
        //     return -1;
        // }
        uart_channel->CR1 |= USART_CR1_TE;
    }
    if (config->mode == UART_MODE_RX || config->mode == UART_MODE_TX_RX) {
        // if (wait_for_ack(uart_channel, USART_ISR_REACK) != 0) {
        //     return -1;
        // }
        uart_channel->CR1 |= USART_CR1_RE;
    }

    uart_initialized[handle->instance] = true;

    return 0;
}

int uart_deinit(const uart_handle_t *handle)
{
    (void)handle;
    return 0;
}

static int uart_write_byte(USART_TypeDef *uart_channel, const uint8_t data)
{

    /* Wait until TDR is empty */
    if (wait_for_ack(uart_channel, USART_ISR_TXE) != 0) {
        return -1;
    }

    uart_channel->TDR = (uint32_t)data;

    return 0;
}

int uart_write_buffer(const uart_handle_t *handle, const uint8_t *data, uint16_t length)
{
    if (handle == NULL || data == NULL || handle->instance >= NUM_OF_UART_PORTS) {
        return -1;
    }

    if (uart_initialized[handle->instance] == false) {
        return -1;
    }

    USART_TypeDef *uart_channel = uart_channels[handle->instance];

    for (uint16_t idx = 0; idx < length; idx++) {
        if (uart_write_byte(uart_channel, data[idx]) != 0) {
            return -1;
        }
    }

    return 0;
}