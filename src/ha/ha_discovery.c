#include "ha_discovery.h"

#include <ctype.h>
#include <stdio.h>
#include <string.h>

int ha_make_slug(const char *display_name, char *out, size_t out_len) {
    if (!display_name || !out || out_len == 0) return -1;
    size_t pos = 0;
    int last_was_underscore = 1;
    for (const char *p = display_name; *p; ++p) {
        unsigned char c = (unsigned char)*p;
        if (isalnum(c)) {
            if (pos + 1 >= out_len) return -1;
            out[pos++] = (char)tolower(c);
            last_was_underscore = 0;
        } else {
            if (!last_was_underscore && pos + 1 < out_len) {
                out[pos++] = '_';
                last_was_underscore = 1;
            }
        }
    }
    /* trim trailing underscore */
    while (pos > 0 && out[pos - 1] == '_') --pos;
    if (pos == 0) return -1;
    if (pos >= out_len) return -1;
    out[pos] = '\0';
    return 0;
}

int ha_build_sensor_config(char *out, size_t out_len,
                           const ha_device_t *dev,
                           const char *key,
                           const char *friendly_name,
                           const char *state_subtopic,
                           const char *unit,
                           const char *device_class,
                           const char *state_class) {
    if (!out || !dev || !key) return -1;
    int n = snprintf(out, out_len,
        "{"
          "\"name\":\"%s\","
          "\"unique_id\":\"ps4_%s_%s\","
          "\"state_topic\":\"ps4/%s/%s\","
          "\"availability_topic\":\"ps4/%s/availability\","
          "\"unit_of_measurement\":\"%s\","
          "\"device_class\":\"%s\","
          "\"state_class\":\"%s\","
          "\"device\":{"
            "\"identifiers\":[\"ps4_%s\"],"
            "\"name\":\"%s\","
            "\"manufacturer\":\"Sony\","
            "\"model\":\"PlayStation 4\","
            "\"sw_version\":\"%s\""
          "}"
        "}",
        friendly_name,
        dev->slug, key,
        dev->slug, state_subtopic,
        dev->slug,
        unit ? unit : "",
        device_class ? device_class : "",
        state_class ? state_class : "",
        dev->slug,
        dev->name,
        dev->fw ? dev->fw : "");
    if (n < 0 || (size_t)n >= out_len) return -1;
    return 0;
}

int ha_build_binary_sensor_config(char *out, size_t out_len,
                                  const ha_device_t *dev,
                                  const char *key,
                                  const char *friendly_name,
                                  const char *state_subtopic,
                                  const char *payload_on,
                                  const char *payload_off) {
    if (!out || !dev || !key) return -1;
    int n = snprintf(out, out_len,
        "{"
          "\"name\":\"%s\","
          "\"unique_id\":\"ps4_%s_%s\","
          "\"state_topic\":\"ps4/%s/%s\","
          "\"availability_topic\":\"ps4/%s/availability\","
          "\"payload_on\":\"%s\","
          "\"payload_off\":\"%s\","
          "\"device\":{"
            "\"identifiers\":[\"ps4_%s\"],"
            "\"name\":\"%s\","
            "\"manufacturer\":\"Sony\","
            "\"model\":\"PlayStation 4\","
            "\"sw_version\":\"%s\""
          "}"
        "}",
        friendly_name,
        dev->slug, key,
        dev->slug, state_subtopic,
        dev->slug,
        payload_on, payload_off,
        dev->slug,
        dev->name,
        dev->fw ? dev->fw : "");
    if (n < 0 || (size_t)n >= out_len) return -1;
    return 0;
}
