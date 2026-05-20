#ifndef PS4MQTT_COLLECTORS_H
#define PS4MQTT_COLLECTORS_H

#include <stdint.h>

typedef struct {
    int64_t uptime_sec;
    uint64_t mem_used_mb;
    uint64_t mem_total_mb;
    char firmware[16];
} system_data_t;

typedef struct {
    double cpu_temp_c;
    double soc_temp_c;
    int    fan_rpm;
} thermal_data_t;

typedef struct {
    char ip[16];
    char ssid[33];
    int  rssi_dbm;
} network_data_t;

typedef struct {
    uint64_t used_gb;
    uint64_t total_gb;
} storage_data_t;

typedef struct {
    int  in_game;            /* 1 if a game is running, 0 otherwise */
    char title[64];          /* "" when not in game */
    char title_id[16];       /* "" when not in game */
    char debug[64];
} app_data_t;

int collect_system (system_data_t  *out);
int collect_thermal(thermal_data_t *out);
int collect_network(network_data_t *out);
int collect_storage(storage_data_t *out);
int collect_app    (app_data_t     *out);

#endif
