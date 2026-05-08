#ifndef PS4MQTT_LOG_H
#define PS4MQTT_LOG_H

typedef enum {
    LOG_LEVEL_ERR   = 0,
    LOG_LEVEL_WARN  = 1,
    LOG_LEVEL_INFO  = 2,
    LOG_LEVEL_DEBUG = 3,
} log_level_t;

void log_init(log_level_t min_level);
void log_write(log_level_t level, const char *fmt, ...);

#define LOG_ERR(...)   log_write(LOG_LEVEL_ERR,   __VA_ARGS__)
#define LOG_WARN(...)  log_write(LOG_LEVEL_WARN,  __VA_ARGS__)
#define LOG_INFO(...)  log_write(LOG_LEVEL_INFO,  __VA_ARGS__)
#define LOG_DEBUG(...) log_write(LOG_LEVEL_DEBUG, __VA_ARGS__)

#endif
