#include "minunit.h"
#include "test_config.c"
#include "test_mqtt_packet.c"
#include "test_ha_discovery.c"

MU_TEST(test_truth) {
    mu_check(1 == 1);
}

MU_TEST_SUITE(suite) {
    MU_RUN_TEST(test_truth);
}

int main(void) {
    MU_RUN_SUITE(suite);
    MU_RUN_SUITE(config_suite);
    MU_RUN_SUITE(mqtt_packet_suite);
    MU_RUN_SUITE(ha_suite);
    MU_REPORT();
    return MU_EXIT_CODE;
}
