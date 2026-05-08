#ifndef PS4MQTT_PUBLISHER_H
#define PS4MQTT_PUBLISHER_H

typedef int (*mqtt_publish_fn)(void *ctx, const char *topic,
                               const char *payload, int retain);

typedef struct {
    const char     *slug;     /* e.g. "sala" */
    mqtt_publish_fn publish;
    void           *ctx;      /* opaque, passed to publish */
} publisher_t;

int publisher_run_once(const publisher_t *pub);

#endif
