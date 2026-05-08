#ifndef PS4MQTT_CONFIG_H
#define PS4MQTT_CONFIG_H

#define CFG_HOST_MAX     64
#define CFG_USER_MAX     32
#define CFG_PASS_MAX     64
#define CFG_DEVNAME_MAX  32

typedef struct {
    char broker_host[CFG_HOST_MAX];
    int  broker_port;
    char username[CFG_USER_MAX];
    char password[CFG_PASS_MAX];
    char device_name[CFG_DEVNAME_MAX];
    int  poll_interval_sec;
} config_t;

/* Returns 0 on success, non-zero on failure. */
int config_load(const char *path, config_t *out);

#endif
