#include "minunit.h"
#include "../src/ha/ha_discovery.h"

#include <string.h>

MU_TEST(test_slug_basic) {
    char out[32];
    int rc = ha_make_slug("Sala de Estar", out, sizeof(out));
    mu_assert_int_eq(0, rc);
    mu_assert_string_eq("sala_de_estar", out);
}

MU_TEST(test_slug_strips_non_alnum) {
    char out[32];
    int rc = ha_make_slug("PS4-Quarto!!", out, sizeof(out));
    mu_assert_int_eq(0, rc);
    mu_assert_string_eq("ps4_quarto", out);
}

MU_TEST(test_slug_buffer_too_small) {
    char out[4];
    int rc = ha_make_slug("Sala Grande", out, sizeof(out));
    mu_check(rc != 0);
}

MU_TEST(test_sensor_config_payload_basics) {
    char out[512];
    ha_device_t dev = {
        .slug = "sala",
        .name = "PS4 Sala",
        .fw   = "11.00",
    };
    int rc = ha_build_sensor_config(out, sizeof(out), &dev,
                                    /*key*/ "cpu_temp",
                                    /*friendly*/ "CPU Temperature",
                                    /*state_subtopic*/ "cpu/temp",
                                    /*unit*/ "\xC2\xB0""C",
                                    /*device_class*/ "temperature",
                                    /*state_class*/ "measurement");
    mu_assert_int_eq(0, rc);
    mu_check(strstr(out, "\"unique_id\":\"ps4_sala_cpu_temp\"") != NULL);
    mu_check(strstr(out, "\"state_topic\":\"ps4/sala/cpu/temp\"") != NULL);
    mu_check(strstr(out, "\"availability_topic\":\"ps4/sala/availability\"") != NULL);
    mu_check(strstr(out, "\"device_class\":\"temperature\"") != NULL);
    mu_check(strstr(out, "\"identifiers\":[\"ps4_sala\"]") != NULL);
    mu_check(strstr(out, "\"sw_version\":\"11.00\"") != NULL);
}

MU_TEST_SUITE(ha_suite) {
    MU_RUN_TEST(test_slug_basic);
    MU_RUN_TEST(test_slug_strips_non_alnum);
    MU_RUN_TEST(test_slug_buffer_too_small);
    MU_RUN_TEST(test_sensor_config_payload_basics);
}
