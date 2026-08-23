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

/** Maximum format arguments per call. Exceeding it is a build error. */
#define LOG_MAX_ARGS 6U

/**
 * @brief Enqueue a log entry. Non-blocking, not ISR-safe.
 *
 * Stores @p module and @p fmt as pointers and copies @p argc argument words;
 * formatting is deferred to logger_flush(). Both strings must therefore have
 * static storage duration — string literals. The same applies to any %s
 * argument, whose pointer is stored rather than its text.
 *
 * Single-producer: must be called only from thread / main-loop context.
 * From an ISR, set a flag and emit the log from the main loop instead.
 * Do not call directly — use the LOG_* macros so the compile-time level
 * filter can strip the call and the argument count is computed for you.
 *
 * @param level  Severity of this entry.
 * @param module Module tag, a string literal.
 * @param argc   Number of format arguments that follow, at most LOG_MAX_ARGS.
 * @param fmt    printf-style format string, a string literal.
 */
void logger_log(log_level_t level, const char *module, uint8_t argc, const char *fmt, ...)
    __attribute__((format(printf, 4, 5)));

/* ------------------------------------------------------------------
 * Argument counting. LOG_COUNT() returns the number of items in
 * __VA_ARGS__ (the format string plus its arguments) by shifting the
 * trailing number list right by that many positions. The
 * LOG_TOO_MANY_ARGS padding is deliberately undeclared: overflowing the
 * list would otherwise select one of the caller's own arguments as the
 * count, silently, so this turns overflow into a build error instead.
 * ------------------------------------------------------------------ */
#define LOG_COUNT(...)                                                                   \
    LOG_COUNT_(__VA_ARGS__, LOG_TOO_MANY_ARGS, LOG_TOO_MANY_ARGS, LOG_TOO_MANY_ARGS,     \
               LOG_TOO_MANY_ARGS, LOG_TOO_MANY_ARGS, LOG_TOO_MANY_ARGS, 7, 6, 5, 4, 3, 2, 1)

#define LOG_COUNT_(_1, _2, _3, _4, _5, _6, _7, _8, _9, _10, _11, _12, _13, N, ...) N

/* The cast keeps -Wconversion quiet: the count expression is unsigned int. */
#define LOG_ARGC(...) ((uint8_t)(LOG_COUNT(__VA_ARGS__) - 1U))

/* ------------------------------------------------------------------
 * Public logging macros — usage:
 *     LOG_INFO("UART", "initialised");
 *     LOG_INFO("GPIO", "pin %u -> %d", pin, val);
 * A call compiles to ((void)0) when LOG_ENABLED=0 or when its level
 * exceeds the current LOG_LEVEL — arguments are not evaluated.
 * ------------------------------------------------------------------ */
#if LOG_ENABLED

#if LOG_LEVEL >= LOG_LEVEL_ERROR
#define LOG_ERROR(module, ...) logger_log(LOG_LEVEL_ERROR, (module), LOG_ARGC(__VA_ARGS__), __VA_ARGS__)
#else
#define LOG_ERROR(module, ...) ((void)0)
#endif

#if LOG_LEVEL >= LOG_LEVEL_WARN
#define LOG_WARN(module, ...) logger_log(LOG_LEVEL_WARN, (module), LOG_ARGC(__VA_ARGS__), __VA_ARGS__)
#else
#define LOG_WARN(module, ...) ((void)0)
#endif

#if LOG_LEVEL >= LOG_LEVEL_INFO
#define LOG_INFO(module, ...) logger_log(LOG_LEVEL_INFO, (module), LOG_ARGC(__VA_ARGS__), __VA_ARGS__)
#else
#define LOG_INFO(module, ...) ((void)0)
#endif

#if LOG_LEVEL >= LOG_LEVEL_DEBUG
#define LOG_DEBUG(module, ...) logger_log(LOG_LEVEL_DEBUG, (module), LOG_ARGC(__VA_ARGS__), __VA_ARGS__)
#else
#define LOG_DEBUG(module, ...) ((void)0)
#endif

#else /* LOG_ENABLED == 0 */

#define LOG_ERROR(module, ...) ((void)0)
#define LOG_WARN(module, ...)  ((void)0)
#define LOG_INFO(module, ...)  ((void)0)
#define LOG_DEBUG(module, ...) ((void)0)

#endif /* LOG_ENABLED */

#endif /* LOGGER_H */
