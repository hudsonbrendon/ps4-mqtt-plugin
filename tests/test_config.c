#include "minunit.h"
#include "../src/config.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static char tmp_path[256];

static void write_tmp(const char *contents) {
    snprintf(tmp_path, sizeof(tmp_path), "/tmp/ps4mqtt_test_%d.json", getpid());
    FILE *f = fopen(tmp_path, "w");
    fputs(contents, f);
    fclose(f);
}

MU_TEST(test_load_full_config) {
    write_tmp("{"
        "\"broker_host\":\"192.168.1.10\","
        "\"broker_port\":1884,"
        "\"username\":\"u\","
        "\"password\":\"p\","
        "\"device_name\":\"Sala\","
        "\"poll_interval_sec\":7"
    "}");
    config_t cfg;
    int rc = config_load(tmp_path, &cfg);
    mu_assert_int_eq(0, rc);
    mu_assert_string_eq("192.168.1.10", cfg.broker_host);
    mu_assert_int_eq(1884, cfg.broker_port);
    mu_assert_string_eq("u", cfg.username);
    mu_assert_string_eq("p", cfg.password);
    mu_assert_string_eq("Sala", cfg.device_name);
    mu_assert_int_eq(7, cfg.poll_interval_sec);
}

MU_TEST(test_defaults_applied_when_optional_missing) {
    write_tmp("{"
        "\"broker_host\":\"10.0.0.1\","
        "\"username\":\"u\","
        "\"password\":\"p\""
    "}");
    config_t cfg;
    int rc = config_load(tmp_path, &cfg);
    mu_assert_int_eq(0, rc);
    mu_assert_int_eq(1883, cfg.broker_port);
    mu_assert_string_eq("PS4", cfg.device_name);
    mu_assert_int_eq(10, cfg.poll_interval_sec);
}

MU_TEST(test_missing_required_field_fails) {
    write_tmp("{\"username\":\"u\",\"password\":\"p\"}");
    config_t cfg;
    int rc = config_load(tmp_path, &cfg);
    mu_check(rc != 0);
}

MU_TEST(test_malformed_json_fails) {
    write_tmp("{not json");
    config_t cfg;
    int rc = config_load(tmp_path, &cfg);
    mu_check(rc != 0);
}

MU_TEST(test_missing_file_fails) {
    config_t cfg;
    int rc = config_load("/tmp/does_not_exist_ps4mqtt.json", &cfg);
    mu_check(rc != 0);
}

MU_TEST_SUITE(config_suite) {
    MU_RUN_TEST(test_load_full_config);
    MU_RUN_TEST(test_defaults_applied_when_optional_missing);
    MU_RUN_TEST(test_missing_required_field_fails);
    MU_RUN_TEST(test_malformed_json_fails);
    MU_RUN_TEST(test_missing_file_fails);
}
