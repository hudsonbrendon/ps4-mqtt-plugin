#include "log.h"
#include "mqtt/mqtt_client.h"
#include "collectors/collectors.h"

#include <pthread.h>
#include <stddef.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

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

#define BROKER_HOST    "192.168.31.150"
#define BROKER_PORT    1883
#define BROKER_USER    "hudsonbrendon"
#define BROKER_PASS    "@Admin996247004"
#define CLIENT_ID      "ps4-mqtt"
#define POLL_INTERVAL  10

static pthread_t      g_thread;
static int            g_thread_started;
static volatile int   g_stop;

static void publish_state(mqtt_client_t *c, long counter) {
    char buf[64];
    mqtt_client_publish(c, "ps4/ps4/availability", "online", 1);
    mqtt_client_publish(c, "ps4/ps4/state",        "on",     1);
    snprintf(buf, sizeof(buf), "%ld", counter);
    mqtt_client_publish(c, "ps4/ps4/heartbeat", buf, 0);

    system_data_t sys;
    if (collect_system(&sys) == 0) {
        snprintf(buf, sizeof(buf), "%lld", (long long)sys.uptime_sec);
        mqtt_client_publish(c, "ps4/ps4/uptime_sec", buf, 0);
        snprintf(buf, sizeof(buf), "%llu", (unsigned long long)sys.mem_used_mb);
        mqtt_client_publish(c, "ps4/ps4/memory/used_mb", buf, 0);
        snprintf(buf, sizeof(buf), "%llu", (unsigned long long)sys.mem_total_mb);
        mqtt_client_publish(c, "ps4/ps4/memory/total_mb", buf, 0);
        mqtt_client_publish(c, "ps4/ps4/firmware", sys.firmware, 1);
    }
}

static void *worker_main(void *arg) {
    (void)arg;
    long counter = 0;

    while (!g_stop) {
        mqtt_client_t *c = mqtt_client_new(BROKER_HOST, BROKER_PORT,
                                           BROKER_USER, BROKER_PASS,
                                           CLIENT_ID, 60,
                                           "ps4/ps4/availability", "offline");
        if (!c) {
            for (int i = 0; i < 30 && !g_stop; ++i) sleep(1);
            continue;
        }
        if (mqtt_client_connect(c) != 0) {
            mqtt_client_free(c);
            for (int i = 0; i < 15 && !g_stop; ++i) sleep(1);
            continue;
        }

        while (!g_stop && mqtt_client_is_connected(c)) {
            publish_state(c, ++counter);
            for (int i = 0; i < POLL_INTERVAL && !g_stop; ++i) sleep(1);
            if (mqtt_client_ping(c) != 0) break;
        }

        mqtt_client_disconnect(c);
        mqtt_client_free(c);
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

    mqtt_client_t *c = mqtt_client_new(BROKER_HOST, BROKER_PORT,
                                       BROKER_USER, BROKER_PASS,
                                       "ps4-mqtt-boot", 60, NULL, NULL);
    if (c) {
        if (mqtt_client_connect(c) == 0) {
            mqtt_client_publish(c, "ps4/ps4/plugin_load_called", "yes", 1);
            mqtt_client_disconnect(c);
        }
        mqtt_client_free(c);
    }

    g_stop = 0;
    int rc = pthread_create(&g_thread, NULL, worker_main, NULL);
    g_thread_started = (rc == 0);

    mqtt_client_t *r = mqtt_client_new(BROKER_HOST, BROKER_PORT,
                                       BROKER_USER, BROKER_PASS,
                                       "ps4-mqtt-boot2", 60, NULL, NULL);
    if (r) {
        if (mqtt_client_connect(r) == 0) {
            char msg[32];
            snprintf(msg, sizeof(msg), "pthread_create=%d", rc);
            mqtt_client_publish(r, "ps4/ps4/thread_status", msg, 1);
            mqtt_client_disconnect(r);
        }
        mqtt_client_free(r);
    }
    return 0;
}

__attribute__((visibility("default")))
int plugin_unload(int argc, const char *argv[]) {
    (void)argc; (void)argv;
    g_stop = 1;
    if (g_thread_started) {
        pthread_join(g_thread, NULL);
        g_thread_started = 0;
    }
    return 0;
}

int module_start(size_t argc, const void *argv) {
    return plugin_load((int)argc, (const char **)argv);
}

int module_stop(size_t argc, const void *argv) {
    return plugin_unload((int)argc, (const char **)argv);
}

int _init(size_t argc, const void *argv) { return module_start(argc, argv); }
int _fini(size_t argc, const void *argv) { return module_stop(argc, argv); }
