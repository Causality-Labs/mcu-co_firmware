#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

#include "logger.h"
#include "ring-buffer.h"
#include "uart.h"

#define LOG_QUEUE_DEPTH 16U

typedef struct
{
    log_level_t level;
    const char *module;
    const char *message;
} log_entry_t;

static log_entry_t log_backing[LOG_QUEUE_DEPTH];
static ring_buffer_t log_queue;

static const uart_instance_t uart_logger = UART_INSTANCE_USART1;

static bool logger_initialized = false;

static const char *level_str(log_level_t level)
{
    switch (level)
    {
    case LOG_LEVEL_ERROR:
        return "ERROR";
    case LOG_LEVEL_WARN:
        return "WARN";
    case LOG_LEVEL_INFO:
        return "INFO";
    case LOG_LEVEL_DEBUG:
        return "DEBUG";
    default:
        return "?";
    }
}

static void emit(const char *s)
{
    while (*s != '\0')
    {
        (void)uart_write_byte(uart_logger, (uint8_t)*s);
        s++;
    }
}

static int init_uart_logger_hw(void)
{
    const uart_config_t config = {
        .baudrate   = UART_BAUD_115200,
        .data_width = UART_DATA_8BIT,
        .parity     = UART_PARITY_NONE,
        .stop_bits  = UART_STOP_1BIT,
        .mode       = UART_MODE_TX,
    };

    if (uart_init(uart_logger, &config, NULL) != 0)
    {
        return -1;
    }

    return 0;
}

int logger_init(void)
{
    if (logger_initialized)
    {
        return -1;
    }

    if (ring_buffer_init(&log_queue, log_backing, LOG_QUEUE_DEPTH, sizeof(log_entry_t)) != STATUS_OK)
    {
        return -1;
    }

    if (init_uart_logger_hw() != 0)
    {
        return -1;
    }

    logger_initialized = true;

    return 0;
}

void logger_log(log_level_t level, const char *module, const char *message)
{
    if (module == NULL || message == NULL)
    {
        return;
    }

    if (!logger_initialized)
    {
        return;
    }

    log_entry_t entry;
    entry.level   = level;
    entry.module  = module;
    entry.message = message;

    (void)ring_buffer_write(&log_queue, &entry, false);
}

void logger_flush(void)
{
    if (!logger_initialized)
    {
        return;
    }

    log_entry_t entry;
    while (ring_buffer_read(&log_queue, &entry) == STATUS_OK)
    {
        emit("[");
        emit(level_str(entry.level));
        emit("] ");
        emit(entry.module);
        emit(": ");
        emit(entry.message);
        emit("\r\n");
    }
}
