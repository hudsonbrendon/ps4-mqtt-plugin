#include "mqtt_packet.h"

#include <string.h>

int mqtt_varint_encode(uint8_t *buf, size_t buflen, uint32_t value) {
    if (value > 268435455u) return -1;
    int written = 0;
    do {
        if ((size_t)written >= buflen) return -1;
        uint8_t byte = (uint8_t)(value & 0x7F);
        value >>= 7;
        if (value > 0) byte |= 0x80;
        buf[written++] = byte;
    } while (value > 0);
    return written;
}

int mqtt_varint_decode(const uint8_t *buf, size_t buflen, uint32_t *out) {
    uint32_t value = 0;
    uint32_t multiplier = 1;
    int consumed = 0;
    for (int i = 0; i < 4; ++i) {
        if ((size_t)i >= buflen) return -1;
        uint8_t byte = buf[i];
        value += (uint32_t)(byte & 0x7F) * multiplier;
        consumed = i + 1;
        if ((byte & 0x80) == 0) {
            *out = value;
            return consumed;
        }
        multiplier *= 128;
    }
    return -1; /* malformed: 5+ bytes */
}

static int write_u16(uint8_t *buf, size_t buflen, size_t pos, uint16_t v) {
    if (pos + 2 > buflen) return -1;
    buf[pos]   = (uint8_t)(v >> 8);
    buf[pos+1] = (uint8_t)(v & 0xFF);
    return 0;
}

static int write_string(uint8_t *buf, size_t buflen, size_t *pos,
                        const char *s) {
    size_t len = strlen(s);
    if (len > 0xFFFF) return -1;
    if (*pos + 2 + len > buflen) return -1;
    buf[(*pos)++] = (uint8_t)(len >> 8);
    buf[(*pos)++] = (uint8_t)(len & 0xFF);
    memcpy(buf + *pos, s, len);
    *pos += len;
    return 0;
}

int mqtt_encode_connect(uint8_t *buf, size_t buflen,
                        const mqtt_connect_opts_t *opts) {
    if (!buf || !opts || !opts->client_id) return -1;

    /* Build payload first to know remaining length. */
    uint8_t payload[512];
    size_t plen = 0;

    if (write_string(payload, sizeof(payload), &plen, opts->client_id) < 0)
        return -1;

    uint8_t flags = 0;
    if (opts->clean_session) flags |= 0x02;
    if (opts->will_topic) {
        flags |= 0x04;
        flags |= (uint8_t)((opts->will_qos & 0x03) << 3);
        if (opts->will_retain) flags |= 0x20;
        if (write_string(payload, sizeof(payload), &plen, opts->will_topic) < 0)
            return -1;
        if (write_string(payload, sizeof(payload), &plen,
                         opts->will_payload ? opts->will_payload : "") < 0)
            return -1;
    }
    if (opts->username) {
        flags |= 0x80;
        if (write_string(payload, sizeof(payload), &plen, opts->username) < 0)
            return -1;
    }
    if (opts->password) {
        flags |= 0x40;
        if (write_string(payload, sizeof(payload), &plen, opts->password) < 0)
            return -1;
    }

    /* Variable header: protocol name + level + flags + keepalive. */
    uint8_t varhdr[] = {
        0x00, 0x04, 'M', 'Q', 'T', 'T',  /* protocol name */
        0x04,                             /* protocol level */
        flags,
        (uint8_t)(opts->keepalive_sec >> 8),
        (uint8_t)(opts->keepalive_sec & 0xFF),
    };

    uint32_t remaining = (uint32_t)(sizeof(varhdr) + plen);

    /* Write the packet. */
    size_t pos = 0;
    if (pos >= buflen) return -1;
    buf[pos++] = 0x10; /* CONNECT control packet type */

    int rl = mqtt_varint_encode(buf + pos, buflen - pos, remaining);
    if (rl < 0) return -1;
    pos += (size_t)rl;

    if (pos + sizeof(varhdr) > buflen) return -1;
    memcpy(buf + pos, varhdr, sizeof(varhdr));
    pos += sizeof(varhdr);

    if (pos + plen > buflen) return -1;
    memcpy(buf + pos, payload, plen);
    pos += plen;

    return (int)pos;
}

int mqtt_encode_publish(uint8_t *buf, size_t buflen,
                        const char *topic,
                        const uint8_t *payload, size_t payload_len,
                        uint8_t qos, int retain) {
    if (!buf || !topic) return -1;
    if (qos != 0) return -1; /* MVP: QoS 0 only */
    size_t topic_len = strlen(topic);
    if (topic_len > 0xFFFF) return -1;

    uint32_t remaining = (uint32_t)(2 + topic_len + payload_len);
    size_t pos = 0;

    if (pos >= buflen) return -1;
    uint8_t header = 0x30; /* PUBLISH, qos 0, dup 0 */
    if (retain) header |= 0x01;
    buf[pos++] = header;

    int rl = mqtt_varint_encode(buf + pos, buflen - pos, remaining);
    if (rl < 0) return -1;
    pos += (size_t)rl;

    if (pos + 2 + topic_len > buflen) return -1;
    buf[pos++] = (uint8_t)(topic_len >> 8);
    buf[pos++] = (uint8_t)(topic_len & 0xFF);
    memcpy(buf + pos, topic, topic_len);
    pos += topic_len;

    if (pos + payload_len > buflen) return -1;
    if (payload_len > 0) {
        memcpy(buf + pos, payload, payload_len);
        pos += payload_len;
    }
    return (int)pos;
}

int mqtt_encode_pingreq(uint8_t *buf, size_t buflen) {
    if (buflen < 2) return -1;
    buf[0] = 0xC0;
    buf[1] = 0x00;
    return 2;
}

int mqtt_encode_disconnect(uint8_t *buf, size_t buflen) {
    if (buflen < 2) return -1;
    buf[0] = 0xE0;
    buf[1] = 0x00;
    return 2;
}

int mqtt_parse_connack(const uint8_t *buf, size_t buflen, uint8_t *return_code) {
    if (!buf || !return_code || buflen < 4) return -1;
    if (buf[0] != 0x20) return -1;
    if (buf[1] != 0x02) return -1;
    *return_code = buf[3];
    return 0;
}

int mqtt_is_pingresp(const uint8_t *buf, size_t buflen) {
    if (!buf || buflen < 2) return 0;
    return (buf[0] == 0xD0 && buf[1] == 0x00) ? 1 : 0;
}
