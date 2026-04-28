#ifndef LOGGER_H
#define LOGGER_H

#include <stdbool.h>

typedef enum {
    LOG_INFO,
    LOG_WARN,
    LOG_ERROR,
    LOG_STATUS
} LogLevel;

/**
 * Initialize the logger with a file path, max size, and debug flag.
 * Must be called before any log_msg() calls.
 * If log_file_path is NULL or empty, file logging is disabled.
 */
void logger_init(const char *log_file_path, int log_max_size_kb, bool debug);

/**
 * Update logger settings (e.g. after config reload).
 */
void logger_configure(const char *log_file_path, int log_max_size_kb, bool debug);

/**
 * Log a message at the given level.
 * - Always writes to the log file (if initialized and log_enabled was true).
 * - Writes to stdout/stderr only if debug=true.
 *
 * Uses printf-style format strings.
 */
void log_msg(LogLevel level, const char *fmt, ...)
    __attribute__((format(printf, 2, 3)));

#endif /* LOGGER_H */
