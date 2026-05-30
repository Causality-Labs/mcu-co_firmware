#ifndef LOGGER_H
#define LOGGER_H

#include <stdint.h>

/* ------------------------------------------------------------------
 * Verbosity levels — defined as preprocessor macros (not enum values)
 * so they can be evaluated in #if at compile time. Higher values are
 * more verbose; LOG_LEVEL_NONE suppresses everything.
 * ------------------------------------------------------------------ */
#define LOG_LEVEL_NONE  0U
#define LOG_LEVEL_ERROR 1U
#define LOG_LEVEL_WARN  2U
#define LOG_LEVEL_INFO  3U
#define LOG_LEVEL_DEBUG 4U

typedef uint8_t log_level_t;

/* ------------------------------------------------------------------
 * Build-time configuration
 *   LOG_ENABLED=0  → strips the entire logger from the build
 *   LOG_LEVEL=<lv> → calls above this level compile to ((void)0)
 * Override from CMake with -DLOG_ENABLED=0 or -DLOG_LEVEL=LOG_LEVEL_WARN.
 * ------------------------------------------------------------------ */
#ifndef LOG_ENABLED
#define LOG_ENABLED 1
#endif

#ifndef LOG_LEVEL
#define LOG_LEVEL LOG_LEVEL_INFO
#endif

/**
 * @brief Initialise the logger.
 *
 * Initialises the bounded log queue and the UART transport.
 *
 * @return 0 on success, -1 on invalid arguments or already initialised.
 */
int logger_init(void);

/**
 * @brief Drain queued log entries to the active transport.
 *
 * Call from the main loop or a low-priority task. Never call from an ISR.
 */
void logger_flush(void);

/**
 * @brief Enqueue a log entry. Non-blocking, not ISR-safe.
 *
 * Single-producer: must be called only from thread / main-loop context.
 * From an ISR, set a flag and emit the log from the main loop instead.
 * Do not call directly — use the LOG_* macros so the compile-time level
 * filter can strip the call.
 */
void logger_log(log_level_t level, const char *module, const char *message);

/* ------------------------------------------------------------------
 * Public logging macros — usage:  LOG_INFO("UART", "initialised");
 * A call compiles to ((void)0) when LOG_ENABLED=0 or when its level
 * exceeds the current LOG_LEVEL — arguments are not evaluated.
 * ------------------------------------------------------------------ */
#if LOG_ENABLED

#if LOG_LEVEL >= LOG_LEVEL_ERROR
#define LOG_ERROR(module, msg) logger_log(LOG_LEVEL_ERROR, (module), (msg))
#else
#define LOG_ERROR(module, msg) ((void)0)
#endif

#if LOG_LEVEL >= LOG_LEVEL_WARN
#define LOG_WARN(module, msg) logger_log(LOG_LEVEL_WARN, (module), (msg))
#else
#define LOG_WARN(module, msg) ((void)0)
#endif

#if LOG_LEVEL >= LOG_LEVEL_INFO
#define LOG_INFO(module, msg) logger_log(LOG_LEVEL_INFO, (module), (msg))
#else
#define LOG_INFO(module, msg) ((void)0)
#endif

#if LOG_LEVEL >= LOG_LEVEL_DEBUG
#define LOG_DEBUG(module, msg) logger_log(LOG_LEVEL_DEBUG, (module), (msg))
#else
#define LOG_DEBUG(module, msg) ((void)0)
#endif

#else /* LOG_ENABLED == 0 */

#define LOG_ERROR(module, msg) ((void)0)
#define LOG_WARN(module, msg)  ((void)0)
#define LOG_INFO(module, msg)  ((void)0)
#define LOG_DEBUG(module, msg) ((void)0)

#endif /* LOG_ENABLED */

#endif /* LOGGER_H */
