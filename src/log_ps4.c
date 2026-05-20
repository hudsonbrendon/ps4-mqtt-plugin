#include "log.h"

#include <stdarg.h>
#include <stdio.h>

extern int sceKernelDebugOutText(int channel, const char *str);

static log_level_t g_min_level = LOG_LEVEL_INFO;

void log_init(log_level_t min_level) {
    g_min_level = min_level;
}

void log_write(log_level_t level, const char *fmt, ...) {
    if (level > g_min_level) return;

    static const char *labels[] = {"ERR", "WARN", "INFO", "DEBUG"};
    char line[512];

    int prefix = snprintf(line, sizeof(line), "[ps4-mqtt][%s] ", labels[level]);
    if (prefix < 0 || prefix >= (int)sizeof(line)) return;

    va_list args;
    va_start(args, fmt);
    int n = vsnprintf(line + prefix, sizeof(line) - prefix - 1, fmt, args);
    va_end(args);
    if (n < 0) return;

    size_t total = (size_t)prefix + (size_t)n;
    if (total >= sizeof(line) - 1) total = sizeof(line) - 2;
    line[total]     = '\n';
    line[total + 1] = '\0';

    sceKernelDebugOutText(0, line);
}
