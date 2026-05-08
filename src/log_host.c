#include "log.h"

#include <stdarg.h>
#include <stdio.h>

static log_level_t g_min_level = LOG_LEVEL_INFO;

void log_init(log_level_t min_level) {
    g_min_level = min_level;
}

void log_write(log_level_t level, const char *fmt, ...) {
    if (level > g_min_level) return;

    static const char *labels[] = {"ERR", "WARN", "INFO", "DEBUG"};
    fprintf(stderr, "[%s] ", labels[level]);

    va_list args;
    va_start(args, fmt);
    vfprintf(stderr, fmt, args);
    va_end(args);

    fputc('\n', stderr);
}
