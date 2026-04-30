#include "logger.h"

#include <stdio.h>
#include <stdarg.h>
#include <string.h>
#include <time.h>
#include <glib.h>

static char   g_log_path[512] = "";
static int    g_log_max_kb    = 1024;
static bool   g_debug         = false;
static GMutex g_logger_mutex;

void logger_init(const char *log_file_path, int log_max_size_kb, bool debug) {
    logger_configure(log_file_path, log_max_size_kb, debug);
}

void logger_configure(const char *log_file_path, int log_max_size_kb, bool debug) {
    g_mutex_lock(&g_logger_mutex);
    if (log_file_path) {
        strncpy(g_log_path, log_file_path, sizeof(g_log_path) - 1);
        g_log_path[sizeof(g_log_path) - 1] = '\0';
    } else {
        g_log_path[0] = '\0';
    }
    g_log_max_kb = log_max_size_kb > 0 ? log_max_size_kb : 1024;
    g_debug = debug;
    g_mutex_unlock(&g_logger_mutex);
}

static const char *level_tag(LogLevel level) {
    switch (level) {
        case LOG_INFO:   return "INFO";
        case LOG_WARN:   return "WARN";
        case LOG_ERROR:  return "ERROR";
        case LOG_STATUS: return "STATUS";
        default:         return "???";
    }
}

void log_msg(LogLevel level, const char *fmt, ...) {
    /* Build timestamp */
    time_t t = time(NULL);
    struct tm tm_buf;
    localtime_r(&t, &tm_buf);
    char ts[64];
    strftime(ts, sizeof(ts), "%Y-%m-%d %H:%M:%S", &tm_buf);

    /* Build user message */
    char body[1024];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(body, sizeof(body), fmt, ap);
    va_end(ap);

    /* Build final line: [2025-01-01 12:00:00] STATUS: message */
    char line[1200];
    snprintf(line, sizeof(line), "[%s] %s: %s", ts, level_tag(level), body);

    g_mutex_lock(&g_logger_mutex);

    /* stdout/stderr when debug is on */
    if (g_debug) {
        if (level == LOG_ERROR) {
            fprintf(stderr, "%s\n", line);
        } else {
            printf("%s\n", line);
        }
        fflush(level == LOG_ERROR ? stderr : stdout);
    }

    /* Write to log file */
    if (g_log_path[0] == '\0') {
        g_mutex_unlock(&g_logger_mutex);
        return;
    }

    FILE *f = fopen(g_log_path, "a");
    if (!f) {
        gchar *dir = g_path_get_dirname(g_log_path);
        g_mkdir_with_parents(dir, 0755);
        g_free(dir);
        f = fopen(g_log_path, "a");
    }

    if (f) {
        fseek(f, 0, SEEK_END);
        if (ftell(f) > g_log_max_kb * 1024) {
            char rotated_path[sizeof(g_log_path) + 3];
            snprintf(rotated_path, sizeof(rotated_path), "%s.1", g_log_path);
            fclose(f);
            remove(rotated_path);
            rename(g_log_path, rotated_path);
            f = fopen(g_log_path, "a");
        }
        if (f) {
            fprintf(f, "%s\n", line);
            fclose(f);
        }
    }

    g_mutex_unlock(&g_logger_mutex);
}
