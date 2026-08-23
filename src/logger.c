#include <stdarg.h>
#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdio.h>

#include "logger.h"
#include "ring-buffer.h"
#include "uart.h"

#define LOG_QUEUE_DEPTH 16U
#define LOG_LINE_MAX    96U

typedef struct
{
    log_level_t level;
    const char *module;
    const char *fmt;
    uint32_t    args[LOG_MAX_ARGS];
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

void logger_log(log_level_t level, const char *module, uint8_t argc, const char *fmt, ...)
{
    if (module == NULL || fmt == NULL)
    {
        return;
    }

    if (!logger_initialized)
    {
        return;
    }

    if (argc > LOG_MAX_ARGS)
    {
        argc = (uint8_t)LOG_MAX_ARGS;
    }

    /* Zero-initialised so unused argument slots are defined when
     * ring_buffer_write() memcpy's the whole struct, and so logger_flush()
     * can pass every slot unconditionally. */
    log_entry_t entry = {0};
    entry.level       = level;
    entry.module      = module;
    entry.fmt         = fmt;

    /* Each argument is pulled as a 32-bit word whatever specifier it belongs
     * to; snprintf reinterprets it at flush time. int, unsigned and pointers
     * all occupy one 4-byte slot on this ABI, which is what makes deferring
     * the formatting legal here. */
    va_list ap;
    va_start(ap, fmt);
    for (uint8_t i = 0U; i < argc; i++)
    {
        entry.args[i] = va_arg(ap, uint32_t);
    }
    va_end(ap);

    (void)ring_buffer_write(&log_queue, &entry, false);
}

void logger_flush(void)
{
    if (!logger_initialized)
    {
        return;
    }

    log_entry_t entry;
    char        line[LOG_LINE_MAX];

    while (ring_buffer_read(&log_queue, &entry) == STATUS_OK)
    {
        emit("[");
        emit(level_str(entry.level));
        emit("] ");
        emit(entry.module);
        emit(": ");

        /* Passing every slot is well-defined even when the format consumes
         * fewer: C99 7.19.6.1 says excess arguments are evaluated and
         * ignored. */
        (void)snprintf(line, sizeof line, entry.fmt, entry.args[0], entry.args[1], entry.args[2],
                       entry.args[3], entry.args[4], entry.args[5]);

        emit(line);
        emit("\r\n");
    }
}
