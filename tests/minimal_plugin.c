#include <stdio.h>

__attribute__((visibility("default"))) const char *g_pluginName    = "minimal";
__attribute__((visibility("default"))) const char *g_pluginDesc    = "minimal test";
__attribute__((visibility("default"))) const char *g_pluginAuth    = "test";
__attribute__((visibility("default"))) unsigned int g_pluginVersion = 0x00000100;

__attribute__((visibility("default")))
int plugin_load(int argc, const char *argv[]) {
    (void)argc; (void)argv;
    FILE *f = fopen("/data/minimal-loaded.txt", "w");
    if (f) { fputs("minimal plugin_load called\n", f); fclose(f); }
    return 0;
}

__attribute__((visibility("default")))
int plugin_unload(int argc, const char *argv[]) {
    (void)argc; (void)argv;
    return 0;
}

__attribute__((weak, visibility("hidden")))
int module_start(size_t argc, const void *argv) { (void)argc; (void)argv; return 0; }

__attribute__((weak, visibility("hidden")))
int module_stop(size_t argc, const void *argv) { (void)argc; (void)argv; return 0; }
