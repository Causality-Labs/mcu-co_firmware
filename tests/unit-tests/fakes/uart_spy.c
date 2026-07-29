/*
 * Link-time substitute for peripherals/uart.c (see uart.h). Records every
 * byte handed to uart_write_byte()/uart_write_buffer(), in order, instead of
 * touching real UART registers.
 */
#include <string.h>
#include "uart_spy.h"

static uint8_t sent_bytes[UART_SPY_MAX_BYTES];
static uint16_t sent_count;

static uart_instance_t last_init_instance;
static uart_config_t last_init_config;

static status_t forced_status;

void UartSpy_Reset(void)
{
    sent_count = 0;
    memset(sent_bytes, 0, sizeof(sent_bytes));
    memset(&last_init_instance, 0, sizeof(last_init_instance));
    memset(&last_init_config, 0, sizeof(last_init_config));
    forced_status = STATUS_OK;
}

void UartSpy_SetReturnStatus(status_t status)
{
    forced_status = status;
}

uint16_t UartSpy_GetSentByteCount(void)
{
    return sent_count;
}

uint8_t UartSpy_GetSentByte(uint16_t index)
{
    return sent_bytes[index];
}

uart_instance_t UartSpy_GetLastInitInstance(void)
{
    return last_init_instance;
}

uart_config_t UartSpy_GetLastInitConfig(void)
{
    return last_init_config;
}

/* --- uart.h implementation --- */

status_t uart_init(uart_instance_t instance, const uart_config_t *config, const uart_rx_buffer_t *rx_buffer)
{
    (void)rx_buffer;
    last_init_instance = instance;
    last_init_config    = *config;
    return forced_status;
}

status_t uart_deinit(uart_instance_t instance)
{
    (void)instance;
    return forced_status;
}

status_t uart_write_byte(uart_instance_t instance, const uint8_t data)
{
    (void)instance;
    if (sent_count < UART_SPY_MAX_BYTES)
    {
        sent_bytes[sent_count] = data;
        sent_count++;
    }
    return forced_status;
}

status_t uart_write_buffer(uart_instance_t instance, const uint8_t *data, uint16_t length)
{
    for (uint16_t i = 0; i < length; i++)
    {
        (void)uart_write_byte(instance, data[i]);
    }
    return forced_status;
}

status_t uart_read_byte(uart_instance_t instance, uint8_t *data)
{
    (void)instance;
    (void)data;
    return STATUS_ERR_EMPTY; /* spy has no simulated RX data */
}

status_t uart_read_buffer(uart_instance_t instance, uint8_t *data, uint16_t length, uint16_t *bytes_read)
{
    (void)instance;
    (void)data;
    (void)length;
    if (bytes_read != NULL)
    {
        *bytes_read = 0;
    }
    return STATUS_OK;
}
