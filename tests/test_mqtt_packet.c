#include "minunit.h"
#include "../src/mqtt/mqtt_packet.h"

#include <string.h>

MU_TEST(test_varint_encode_one_byte) {
    uint8_t buf[4];
    int n = mqtt_varint_encode(buf, sizeof(buf), 0);
    mu_assert_int_eq(1, n);
    mu_assert_int_eq(0x00, buf[0]);

    n = mqtt_varint_encode(buf, sizeof(buf), 127);
    mu_assert_int_eq(1, n);
    mu_assert_int_eq(0x7F, buf[0]);
}

MU_TEST(test_varint_encode_two_bytes) {
    uint8_t buf[4];
    int n = mqtt_varint_encode(buf, sizeof(buf), 128);
    mu_assert_int_eq(2, n);
    mu_assert_int_eq(0x80, buf[0]);
    mu_assert_int_eq(0x01, buf[1]);

    n = mqtt_varint_encode(buf, sizeof(buf), 16383);
    mu_assert_int_eq(2, n);
    mu_assert_int_eq(0xFF, buf[0]);
    mu_assert_int_eq(0x7F, buf[1]);
}

MU_TEST(test_varint_encode_four_bytes) {
    uint8_t buf[4];
    int n = mqtt_varint_encode(buf, sizeof(buf), 268435455);
    mu_assert_int_eq(4, n);
    mu_assert_int_eq(0xFF, buf[0]);
    mu_assert_int_eq(0xFF, buf[1]);
    mu_assert_int_eq(0xFF, buf[2]);
    mu_assert_int_eq(0x7F, buf[3]);
}

MU_TEST(test_varint_decode_round_trip) {
    uint32_t values[] = {0, 1, 127, 128, 16383, 16384, 2097151, 268435455};
    for (size_t i = 0; i < sizeof(values)/sizeof(values[0]); ++i) {
        uint8_t buf[4];
        int n = mqtt_varint_encode(buf, sizeof(buf), values[i]);
        mu_check(n > 0);
        uint32_t decoded = 0;
        int consumed = mqtt_varint_decode(buf, (size_t)n, &decoded);
        mu_assert_int_eq(n, consumed);
        mu_check(decoded == values[i]);
    }
}

MU_TEST(test_varint_encode_buffer_too_small) {
    uint8_t buf[1];
    int n = mqtt_varint_encode(buf, sizeof(buf), 200);
    mu_check(n < 0);
}

MU_TEST(test_connect_basic_fields) {
    uint8_t buf[256];
    mqtt_connect_opts_t opts = {
        .client_id = "ps4-sala",
        .username  = "u",
        .password  = "p",
        .keepalive_sec = 60,
        .clean_session = 1,
        .will_topic = "ps4/sala/availability",
        .will_payload = "offline",
        .will_qos = 1,
        .will_retain = 1,
    };
    int n = mqtt_encode_connect(buf, sizeof(buf), &opts);
    mu_check(n > 0);

    /* fixed header byte 1 = 0x10 (CONNECT) */
    mu_assert_int_eq(0x10, buf[0]);

    /* variable header begins after fixed header.
     * We expect: protocol name length 0x00 0x04 then "MQTT". */
    /* Skip remaining-length varint (1 or 2 bytes). */
    int idx = 1;
    uint32_t remlen = 0;
    int rl_consumed = mqtt_varint_decode(buf + idx, sizeof(buf) - idx, &remlen);
    mu_check(rl_consumed > 0);
    idx += rl_consumed;

    mu_assert_int_eq(0x00, buf[idx++]);
    mu_assert_int_eq(0x04, buf[idx++]);
    mu_assert_int_eq('M', buf[idx++]);
    mu_assert_int_eq('Q', buf[idx++]);
    mu_assert_int_eq('T', buf[idx++]);
    mu_assert_int_eq('T', buf[idx++]);
    /* protocol level = 4 */
    mu_assert_int_eq(0x04, buf[idx++]);
    /* connect flags: clean session(0x02) + will(0x04) + will_qos1(0x08)
       + will_retain(0x20) + password(0x40) + username(0x80) = 0xEE */
    mu_assert_int_eq(0xEE, buf[idx++]);
    /* keepalive 60 = 0x00 0x3C */
    mu_assert_int_eq(0x00, buf[idx++]);
    mu_assert_int_eq(0x3C, buf[idx]);
}

MU_TEST(test_connect_buffer_too_small) {
    uint8_t buf[10];
    mqtt_connect_opts_t opts = {
        .client_id = "ps4-sala",
        .username = "u", .password = "p",
        .keepalive_sec = 60, .clean_session = 1,
    };
    int n = mqtt_encode_connect(buf, sizeof(buf), &opts);
    mu_check(n < 0);
}

MU_TEST(test_publish_qos0_no_retain) {
    uint8_t buf[64];
    int n = mqtt_encode_publish(buf, sizeof(buf), "ps4/sala/state",
                                (const uint8_t *)"on", 2,
                                /*qos*/ 0, /*retain*/ 0);
    mu_check(n > 0);
    /* fixed header byte: 0x30 = PUBLISH, qos 0, no retain, no dup */
    mu_assert_int_eq(0x30, buf[0]);

    int idx = 1;
    uint32_t remlen = 0;
    int rl = mqtt_varint_decode(buf + idx, sizeof(buf) - idx, &remlen);
    idx += rl;
    /* topic length = 14 ("ps4/sala/state") */
    mu_assert_int_eq(0x00, buf[idx++]);
    mu_assert_int_eq(0x0E, buf[idx++]);
    mu_check(memcmp(buf + idx, "ps4/sala/state", 14) == 0);
    idx += 14;
    mu_check(memcmp(buf + idx, "on", 2) == 0);

    /* remaining length = 2 (topic length bytes) + 14 (topic) + 2 (payload) = 18 */
    mu_check(remlen == 18);
}

MU_TEST(test_publish_retain_flag) {
    uint8_t buf[64];
    int n = mqtt_encode_publish(buf, sizeof(buf), "x",
                                (const uint8_t *)"y", 1,
                                /*qos*/ 0, /*retain*/ 1);
    mu_check(n > 0);
    /* 0x30 | 0x01 = 0x31 (retain bit) */
    mu_assert_int_eq(0x31, buf[0]);
}

MU_TEST_SUITE(mqtt_packet_suite) {
    MU_RUN_TEST(test_varint_encode_one_byte);
    MU_RUN_TEST(test_varint_encode_two_bytes);
    MU_RUN_TEST(test_varint_encode_four_bytes);
    MU_RUN_TEST(test_varint_decode_round_trip);
    MU_RUN_TEST(test_varint_encode_buffer_too_small);
    MU_RUN_TEST(test_connect_basic_fields);
    MU_RUN_TEST(test_connect_buffer_too_small);
    MU_RUN_TEST(test_publish_qos0_no_retain);
    MU_RUN_TEST(test_publish_retain_flag);
}
