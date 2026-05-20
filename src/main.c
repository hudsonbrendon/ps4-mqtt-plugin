#include "log.h"
#include "mqtt/mqtt_client.h"

#include <stddef.h>
#include <stdio.h>
#include <string.h>

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

__attribute__((visibility("default"))) const char *g_pluginName    = "ps4-mqtt";
__attribute__((visibility("default"))) const char *g_pluginDesc    = "PS4 telemetry to MQTT/Home Assistant";
__attribute__((visibility("default"))) const char *g_pluginAuth    = "hudsonbrendon";
__attribute__((visibility("default"))) unsigned int g_pluginVersion = 0x00000100;

__attribute__((visibility("default")))
int plugin_load(int argc, const char *argv[]) {
    (void)argc; (void)argv;

    mqtt_client_t *c = mqtt_client_new("192.168.31.150", 1883,
                                       "hudsonbrendon", "@Admin996247004",
                                       "ps4-mqtt-test", 60,
                                       "ps4/ps4/availability", "offline");
    if (!c) return 0;
    if (mqtt_client_connect(c) == 0) {
        mqtt_client_publish(c, "ps4/ps4/availability", "online", 1);
        mqtt_client_publish(c, "ps4/ps4/state", "on", 1);
        mqtt_client_publish(c, "ps4/test", "hello-from-real-plugin", 1);
        mqtt_client_disconnect(c);
    }
    mqtt_client_free(c);
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
