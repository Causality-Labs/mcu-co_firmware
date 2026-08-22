/*
 * Link-time substitute for peripherals/uart.c (see uart.h). Records every
 * byte handed to uart_write_byte()/uart_write_buffer(), in order, instead of
 * touching real UART registers.
 */
#include <stdbool.h>
#include <string.h>
#include "uart_spy.h"

static uint8_t sent_bytes[UART_SPY_MAX_BYTES];
static uint16_t sent_count;

static uart_instance_t last_init_instance;
static uart_config_t last_init_config;

static uint16_t deinit_call_count;
static uart_instance_t last_deinit_instance;

static uart_instance_t last_read_byte_instance;
static bool read_byte_available;
static uint8_t next_read_byte;

static status_t forced_status;

void UartSpy_Reset(void)
{
    sent_count = 0;
    memset(sent_bytes, 0, sizeof(sent_bytes));
    memset(&last_init_instance, 0, sizeof(last_init_instance));
    memset(&last_init_config, 0, sizeof(last_init_config));
    deinit_call_count = 0;
    memset(&last_deinit_instance, 0, sizeof(last_deinit_instance));
    memset(&last_read_byte_instance, 0, sizeof(last_read_byte_instance));
    read_byte_available = false;
    next_read_byte = 0;
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

uint16_t UartSpy_GetDeinitCallCount(void)
{
    return deinit_call_count;
}

uart_instance_t UartSpy_GetLastDeinitInstance(void)
{
    return last_deinit_instance;
}

uart_instance_t UartSpy_GetLastReadByteInstance(void)
{
    return last_read_byte_instance;
}

void UartSpy_SetNextReadByte(uint8_t byte)
{
    next_read_byte = byte;
    read_byte_available = true;
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
    deinit_call_count++;
    last_deinit_instance = instance;
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
    last_read_byte_instance = instance;

    if (!read_byte_available)
    {
        return STATUS_ERR_EMPTY;
    }

    *data = next_read_byte;
    read_byte_available = false;

    return STATUS_OK;
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
