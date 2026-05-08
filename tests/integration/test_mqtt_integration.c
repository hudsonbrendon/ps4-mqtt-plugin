#include "minunit.h"
#include "../../src/mqtt/mqtt_client.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

/* Requires a local Mosquitto on 127.0.0.1:1883 with auth. The wrapper
 * run_integration.sh sets this up. */

static const char *BROKER = "127.0.0.1";
static const int   PORT   = 1883;
static const char *USER   = "test_user";
static const char *PASS   = "test_pass";

MU_TEST(test_connect_publish_disconnect) {
    mqtt_client_t *c = mqtt_client_new(BROKER, PORT, USER, PASS,
                                       /*client_id*/ "ps4-mqtt-test",
                                       /*keepalive_sec*/ 30,
                                       /*will_topic*/ "ps4/test/availability",
                                       /*will_payload*/ "offline");
    mu_check(c != NULL);

    int rc = mqtt_client_connect(c);
    mu_assert_int_eq(0, rc);

    rc = mqtt_client_publish(c, "ps4/test/state", "on", /*retain*/ 1);
    mu_assert_int_eq(0, rc);

    rc = mqtt_client_disconnect(c);
    mu_assert_int_eq(0, rc);

    mqtt_client_free(c);
}

MU_TEST_SUITE(integration_suite) {
    MU_RUN_TEST(test_connect_publish_disconnect);
}

int main(void) {
    MU_RUN_SUITE(integration_suite);
    MU_REPORT();
    return MU_EXIT_CODE;
}
