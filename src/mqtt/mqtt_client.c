#include "mqtt_client.h"
#include "mqtt_packet.h"
#include "mqtt_socket.h"
#include "../log.h"

#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#define BUF_MAX 1024

struct mqtt_client {
    char host[64];
    int  port;
    char user[32];
    char pass[64];
    char client_id[64];
    int  keepalive_sec;
    char will_topic[96];
    char will_payload[64];
    int  fd;
    int  connected;
};

static void copy_or_empty(char *dst, size_t cap, const char *src) {
    if (!src) { dst[0] = '\0'; return; }
    strncpy(dst, src, cap - 1);
    dst[cap - 1] = '\0';
}

mqtt_client_t *mqtt_client_new(const char *host, int port,
                               const char *user, const char *pass,
                               const char *client_id,
                               int keepalive_sec,
                               const char *will_topic,
                               const char *will_payload) {
    if (!host || !client_id) return NULL;
    mqtt_client_t *c = calloc(1, sizeof(*c));
    if (!c) return NULL;
    copy_or_empty(c->host,         sizeof(c->host),         host);
    c->port = port;
    copy_or_empty(c->user,         sizeof(c->user),         user);
    copy_or_empty(c->pass,         sizeof(c->pass),         pass);
    copy_or_empty(c->client_id,    sizeof(c->client_id),    client_id);
    c->keepalive_sec = keepalive_sec;
    copy_or_empty(c->will_topic,   sizeof(c->will_topic),   will_topic);
    copy_or_empty(c->will_payload, sizeof(c->will_payload), will_payload);
    c->fd = -1;
    return c;
}

int mqtt_client_connect(mqtt_client_t *c) {
    if (!c) return -1;
    c->fd = mqtt_socket_connect(c->host, c->port);
    if (c->fd < 0) {
        LOG_WARN("mqtt: socket connect failed to %s:%d", c->host, c->port);
        return -1;
    }

    uint8_t buf[BUF_MAX];
    mqtt_connect_opts_t opts = {
        .client_id     = c->client_id,
        .username      = c->user[0] ? c->user : NULL,
        .password      = c->pass[0] ? c->pass : NULL,
        .keepalive_sec = (uint16_t)c->keepalive_sec,
        .clean_session = 1,
    };
    if (c->will_topic[0]) {
        opts.will_topic   = c->will_topic;
        opts.will_payload = c->will_payload;
        opts.will_qos     = 1;
        opts.will_retain  = 1;
    }
    int n = mqtt_encode_connect(buf, sizeof(buf), &opts);
    if (n <= 0) goto fail;
    if (mqtt_socket_send(c->fd, buf, (size_t)n) != n) goto fail;

    int got = mqtt_socket_recv(c->fd, buf, sizeof(buf), 5000);
    if (got <= 0) {
        LOG_WARN("mqtt: no CONNACK within 5 s");
        goto fail;
    }
    uint8_t rc = 0xFF;
    if (mqtt_parse_connack(buf, (size_t)got, &rc) != 0) {
        LOG_WARN("mqtt: malformed CONNACK");
        goto fail;
    }
    if (rc != 0) {
        LOG_WARN("mqtt: broker rejected connection (rc=0x%02X)", rc);
        goto fail;
    }
    c->connected = 1;
    LOG_INFO("mqtt: connected to %s:%d as %s", c->host, c->port, c->client_id);
    return 0;

fail:
    mqtt_socket_close(c->fd);
    c->fd = -1;
    c->connected = 0;
    return -1;
}

int mqtt_client_publish(mqtt_client_t *c, const char *topic,
                        const char *payload, int retain) {
    if (!c || !c->connected) return -1;
    uint8_t buf[BUF_MAX];
    size_t plen = payload ? strlen(payload) : 0;
    int n = mqtt_encode_publish(buf, sizeof(buf), topic,
                                (const uint8_t *)payload, plen,
                                /*qos*/ 0, retain);
    if (n <= 0) return -1;
    if (mqtt_socket_send(c->fd, buf, (size_t)n) != n) {
        LOG_WARN("mqtt: publish send failed");
        c->connected = 0;
        return -1;
    }
    return 0;
}

int mqtt_client_ping(mqtt_client_t *c) {
    if (!c || !c->connected) return -1;
    uint8_t buf[2];
    int n = mqtt_encode_pingreq(buf, sizeof(buf));
    if (mqtt_socket_send(c->fd, buf, (size_t)n) != n) return -1;
    int got = mqtt_socket_recv(c->fd, buf, sizeof(buf), 3000);
    if (got <= 0 || !mqtt_is_pingresp(buf, (size_t)got)) {
        c->connected = 0;
        return -1;
    }
    return 0;
}

int mqtt_client_disconnect(mqtt_client_t *c) {
    if (!c) return -1;
    if (c->connected && c->fd >= 0) {
        uint8_t buf[2];
        int n = mqtt_encode_disconnect(buf, sizeof(buf));
        mqtt_socket_send(c->fd, buf, (size_t)n);
        uint8_t drain[16];
        mqtt_socket_recv(c->fd, drain, sizeof(drain), 1000);
    }
    if (c->fd >= 0) mqtt_socket_close(c->fd);
    c->fd = -1;
    c->connected = 0;
    return 0;
}

int mqtt_client_is_connected(const mqtt_client_t *c) {
    return c ? c->connected : 0;
}

void mqtt_client_free(mqtt_client_t *c) {
    if (!c) return;
    mqtt_client_disconnect(c);
    free(c);
}
