#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

#include "logger.h"
#include "ring-buffer.h"
#include "uart.h"

#define LOG_QUEUE_DEPTH 16U

typedef struct {
    log_level_t level;
    const char *module;
    const char *message;
} log_entry_t;

static log_entry_t log_backing[LOG_QUEUE_DEPTH];
static ring_buffer_t log_queue;

static bool transport_initialized[NUM_OF_LOG_TRANSPORT] = {
    [LOG_TRANSPORT_UART] = false,
    [LOG_TRANSPORT_ITM]  = false,
};
static const uart_instance_t uart_logger = UART_INSTANCE_USART1;

static bool logger_queue_initialized = false;

static const char *level_str(log_level_t level)
{
    switch (level) {
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
    while (*s != '\0') {
        if (transport_initialized[LOG_TRANSPORT_UART]) {
            (void)uart_write_byte(uart_logger, (uint8_t)*s);
        }
        if (transport_initialized[LOG_TRANSPORT_ITM]) {
            (void)ITM_SendChar((uint32_t)(uint8_t)*s);
        }
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

    if (uart_init(uart_logger, &config, NULL) != 0) {
        return -1;
    }

    return 0;
}

int logger_init(log_transport_t transport)
{

    if (transport_initialized[transport]) {
        return -1;
    }

    if (!logger_queue_initialized) {
        if (ring_buffer_init(&log_queue, log_backing, LOG_QUEUE_DEPTH, sizeof(log_entry_t)) != 0) {
            return -1;
        }
    }

    switch (transport) {
        case LOG_TRANSPORT_UART:
            /* Caller must init the UART peripheral separately. */
            if (init_uart_logger_hw() != 0) {
                return -1;
            }
            transport_initialized[transport] = true;

            break;

        case LOG_TRANSPORT_ITM:
            /* Usually the debugger enables ITM, nothing to do here yet. */
            break;

        default:
            return -1;
    }

    logger_queue_initialized = true;

    return 0;
}

void logger_log(log_level_t level, const char *module, const char *message)
{
    if (module == NULL || message == NULL) {
        return;
    }

    if (!logger_queue_initialized) {
        return;
    }

    log_entry_t entry;
    entry.level   = level;
    entry.module  = module;
    entry.message = message;

    (void)ring_buffer_write(&log_queue, &entry);
}

void logger_flush(void)
{
    if (!logger_queue_initialized) {
        return;
    }

    log_entry_t entry;
    while (ring_buffer_read(&log_queue, &entry) == 0) {
        emit("[");
        emit(level_str(entry.level));
        emit("] ");
        emit(entry.module);
        emit(": ");
        emit(entry.message);
        emit("\r\n");
    }
}
