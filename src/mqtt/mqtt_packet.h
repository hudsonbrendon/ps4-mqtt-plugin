#ifndef PS4MQTT_MQTT_PACKET_H
#define PS4MQTT_MQTT_PACKET_H

#include <stddef.h>
#include <stdint.h>

/* MQTT 3.1.1 variable-length integer (spec §2.2.3).
 * Returns bytes written (1..4) on success, or -1 on error
 * (buffer too small or value > 268435455). */
int mqtt_varint_encode(uint8_t *buf, size_t buflen, uint32_t value);

/* Decodes a variable-length integer from buf.
 * Returns bytes consumed on success, or -1 on error. */
int mqtt_varint_decode(const uint8_t *buf, size_t buflen, uint32_t *out);

typedef struct {
    const char *client_id;       /* required */
    const char *username;        /* NULL to omit */
    const char *password;        /* NULL to omit */
    uint16_t    keepalive_sec;
    int         clean_session;
    const char *will_topic;      /* NULL to omit will */
    const char *will_payload;
    uint8_t     will_qos;
    int         will_retain;
} mqtt_connect_opts_t;

int mqtt_encode_connect(uint8_t *buf, size_t buflen,
                        const mqtt_connect_opts_t *opts);

#endif
