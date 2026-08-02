#include <stdbool.h>
#include <stddef.h>
#include "command_transport.h"

#define COMMAND_TRANSPORT_RX_BUFFER_SIZE 128U

static uint8_t rx_buffer_storage[COMMAND_TRANSPORT_RX_BUFFER_SIZE];
static uart_rx_buffer_t rx_buffer = {
    .buffer = rx_buffer_storage,
    .size   = COMMAND_TRANSPORT_RX_BUFFER_SIZE,
};

static uart_instance_t transport_instance;
static bool initialized = false;

status_t command_transport_init(uart_instance_t instance)
{
    const uart_config_t config = {
        .baudrate   = UART_BAUD_115200,
        .data_width = UART_DATA_8BIT,
        .parity     = UART_PARITY_NONE,
        .stop_bits  = UART_STOP_1BIT,
        .mode       = UART_MODE_TX_RX,
    };

    status_t status = uart_init(instance, &config, &rx_buffer);
    if (status != STATUS_OK)
    {
        return status;
    }

    transport_instance = instance;
    initialized        = true;

    return STATUS_OK;
}

status_t command_transport_deinit(void)
{
    if (!initialized)
    {
        return STATUS_OK;
    }

    status_t status = uart_deinit(transport_instance);
    initialized     = false;

    return status;
}

status_t command_transport_receive(uint8_t *data)
{
    if (!initialized)
    {
        return STATUS_ERR_NOT_INIT;
    }

    if (data == NULL)
    {
        return STATUS_ERR_INVALID_ARG;
    }

    return uart_read_byte(transport_instance, data);
}

status_t command_transport_send_response(const uint8_t *frame, uint16_t length)
{
    if (!initialized)
    {
        return STATUS_ERR_NOT_INIT;
    }

    if (frame == NULL)
    {
        return STATUS_ERR_INVALID_ARG;
    }

    return uart_write_buffer(transport_instance, frame, length);
}
