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

MU_TEST_SUITE(mqtt_packet_suite) {
    MU_RUN_TEST(test_varint_encode_one_byte);
    MU_RUN_TEST(test_varint_encode_two_bytes);
    MU_RUN_TEST(test_varint_encode_four_bytes);
    MU_RUN_TEST(test_varint_decode_round_trip);
    MU_RUN_TEST(test_varint_encode_buffer_too_small);
}
