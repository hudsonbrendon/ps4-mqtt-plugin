#include "mqtt_packet.h"

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
