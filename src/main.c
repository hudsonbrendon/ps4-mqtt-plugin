#include "config.h"
#include "log.h"
#include "publisher.h"
#include "ha/ha_discovery.h"
#include "mqtt/mqtt_client.h"

#include <pthread.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#define CONFIG_PATH "/data/GoldHEN/plugins/ps4-mqtt/config.json"

__asm__(
    ".intel_syntax noprefix \n"
    ".align 0x8 \n"
    ".section \".data.sce_module_param\" \n"
    "_sceProcessParam: \n"
    "    .quad 0x18 \n"
    "    .quad 0x13C13F4BF \n"
    "    .quad 0x4508101 \n"
    ".att_syntax prefix \n"
);

__asm__(
    ".intel_syntax noprefix \n"
    ".align 0x8 \n"
    ".data \n"
    "__dso_handle: \n"
    "    .quad 0 \n"
    "_sceLibc: \n"
    "    .quad 0 \n"
    ".att_syntax prefix \n"
);

static pthread_t      g_thread;
static int            g_thread_started;
static volatile int   g_stop;
static mqtt_client_t *g_client;
static config_t       g_cfg;
static char           g_slug[CFG_DEVNAME_MAX];

static int mqtt_publish_adapter(void *ctx, const char *topic,
                                const char *payload, int retain) {
    return mqtt_client_publish((mqtt_client_t *)ctx, topic, payload, retain);
}

static void publish_discovery(mqtt_client_t *c, const char *slug,
                              const char *device_name, const char *fw) {
    ha_device_t dev = { .slug = slug, .name = device_name, .fw = fw };
    char topic[160];
    char payload[768];

    struct sensor_def {
        const char *key;
        const char *friendly;
        const char *subtopic;
        const char *unit;
        const char *device_class;
        const char *state_class;
    };
    static const struct sensor_def sensors[] = {
        {"cpu_temp",      "CPU Temperature",  "cpu/temp",        "\xC2\xB0""C", "temperature",     "measurement"},
        {"soc_temp",      "SoC Temperature",  "soc/temp",        "\xC2\xB0""C", "temperature",     "measurement"},
        {"fan_rpm",       "Fan Speed",        "fan/rpm",         "rpm",         "",                "measurement"},
        {"memory_used",   "Memory Used",      "memory/used_mb",  "MB",          "data_size",       "measurement"},
        {"memory_total",  "Memory Total",     "memory/total_mb", "MB",          "data_size",       "measurement"},
        {"network_rssi",  "WiFi Signal",      "network/rssi",    "dBm",         "signal_strength", "measurement"},
        {"storage_used",  "Storage Used",     "storage/used_gb", "GB",          "data_size",       "measurement"},
        {"storage_total", "Storage Total",    "storage/total_gb","GB",          "data_size",       "measurement"},
        {"uptime_sec",    "Uptime",           "uptime_sec",      "s",           "duration",        "total_increasing"},
    };

    for (size_t i = 0; i < sizeof(sensors)/sizeof(sensors[0]); ++i) {
        const struct sensor_def *s = &sensors[i];
        snprintf(topic, sizeof(topic),
                 "homeassistant/sensor/ps4_%s_%s/config", slug, s->key);
        if (ha_build_sensor_config(payload, sizeof(payload), &dev,
                                   s->key, s->friendly, s->subtopic,
                                   s->unit, s->device_class, s->state_class) == 0) {
            mqtt_client_publish(c, topic, payload, /*retain*/ 1);
        }
    }

    snprintf(topic, sizeof(topic),
             "homeassistant/binary_sensor/ps4_%s_state/config", slug);
    if (ha_build_binary_sensor_config(payload, sizeof(payload), &dev,
                                      "state", "PS4 State", "state",
                                      "on", "standby") == 0) {
        mqtt_client_publish(c, topic, payload, 1);
    }
}

static void *worker_main(void *arg) {
    (void)arg;
    while (!g_stop) {
        if (!mqtt_client_is_connected(g_client)) {
            static const int delays[] = {5, 10, 30, 60};
            for (size_t i = 0; !g_stop; ++i) {
                if (mqtt_client_connect(g_client) == 0) {
                    char avail_topic[128];
                    snprintf(avail_topic, sizeof(avail_topic),
                             "ps4/%s/availability", g_slug);
                    mqtt_client_publish(g_client, avail_topic, "online", 1);
                    publish_discovery(g_client, g_slug,
                                      g_cfg.device_name, "11.00");
                    break;
                }
                int d = delays[i < 4 ? i : 3];
                for (int s = 0; s < d && !g_stop; ++s) sleep(1);
            }
            continue;
        }

        publisher_t pub = {
            .slug    = g_slug,
            .publish = mqtt_publish_adapter,
            .ctx     = g_client,
        };
        publisher_run_once(&pub);

        for (int s = 0; s < g_cfg.poll_interval_sec && !g_stop; ++s) {
            sleep(1);
        }
        mqtt_client_ping(g_client);
    }
    return NULL;
}

__attribute__((visibility("default"))) const char *g_pluginName    = "ps4-mqtt";
__attribute__((visibility("default"))) const char *g_pluginDesc    = "PS4 telemetry to MQTT/Home Assistant";
__attribute__((visibility("default"))) const char *g_pluginAuth    = "hudsonbrendon";
__attribute__((visibility("default"))) unsigned int g_pluginVersion = 0x00000100;

__attribute__((visibility("default")))
int plugin_load(int argc, const char *argv[]) {
    (void)argc; (void)argv;
    FILE *marker = fopen("/data/ps4-mqtt-loaded.txt", "w");
    if (marker) { fputs("plugin_load was called\n", marker); fclose(marker); }
    return 0;
}

__attribute__((visibility("default")))
int plugin_unload(int argc, const char *argv[]) {
    (void)argc; (void)argv;
    return 0;
}

int module_start(size_t argc, const void *argv) {
    return plugin_load((int)argc, (const char **)argv);
}

int module_stop(size_t argc, const void *argv) {
    return plugin_unload((int)argc, (const char **)argv);
}

int _init(size_t argc, const void *argv) {
    return module_start(argc, argv);
}

int _fini(size_t argc, const void *argv) {
    return module_stop(argc, argv);
}
