#ifndef PS4MQTT_HA_DISCOVERY_H
#define PS4MQTT_HA_DISCOVERY_H

#include <stddef.h>

typedef struct {
    const char *slug;   /* e.g. "sala"                 */
    const char *name;   /* e.g. "PS4 Sala"             */
    const char *fw;     /* e.g. "11.00"                */
} ha_device_t;

int ha_make_slug(const char *display_name, char *out, size_t out_len);

int ha_build_sensor_config(char *out, size_t out_len,
                           const ha_device_t *dev,
                           const char *key,
                           const char *friendly_name,
                           const char *state_subtopic,
                           const char *unit,
                           const char *device_class,
                           const char *state_class);

int ha_build_binary_sensor_config(char *out, size_t out_len,
                                  const ha_device_t *dev,
                                  const char *key,
                                  const char *friendly_name,
                                  const char *state_subtopic,
                                  const char *payload_on,
                                  const char *payload_off);

#endif
