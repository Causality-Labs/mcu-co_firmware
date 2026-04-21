#ifndef LOGGER_H
#define LOGGER_H

#include <stdint.h>
#include "uart.h"

// /** @brief Verbosity levels — messages below the active level are suppressed. */
// typedef enum {
//     LOG_LEVEL_NONE  = 0U,
//     LOG_LEVEL_ERROR = 1U,
//     LOG_LEVEL_WARN  = 2U,
//     LOG_LEVEL_INFO  = 3U,
//     LOG_LEVEL_DEBUG = 4U,
// } log_level_t;

// /**
//  * @brief Initialise the logger.
//  *
//  * @param level    Minimum verbosity level to emit
//  * @return 0 on success, -1 on failure
//  */
// int log_init();

// /**
//  * @brief Emit a log message at the given level.
//  *
//  * Suppressed silently if @p level is below the active verbosity level.
//  *
//  * @param level   Severity of the message
//  * @param message Null-terminated string to transmit
//  */
// void logger_log(log_level_t level, const char *message);

// /** @brief Convenience wrappers. */
// void logger_error(const char *message);
// void logger_warn(const char *message);
// void logger_info(const char *message);
// void logger_debug(const char *message);

#endif /* LOGGER_H */
