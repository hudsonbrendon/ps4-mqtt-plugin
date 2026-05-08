#include "minunit.h"
#include "../src/publisher.h"
#include "../src/collectors/collectors.h"

#include <string.h>

typedef struct { char topic[128]; char payload[256]; int retain; } pub_call_t;
static pub_call_t calls[64];
static int calls_n = 0;

static int fake_publish(void *unused, const char *topic,
                        const char *payload, int retain) {
    (void)unused;
    if (calls_n >= 64) return -1;
    strncpy(calls[calls_n].topic,   topic,   sizeof(calls[0].topic)   - 1);
    strncpy(calls[calls_n].payload, payload, sizeof(calls[0].payload) - 1);
    calls[calls_n].retain = retain;
    calls_n++;
    return 0;
}

static const pub_call_t *find_call(const char *topic) {
    for (int i = 0; i < calls_n; ++i)
        if (strcmp(calls[i].topic, topic) == 0) return &calls[i];
    return NULL;
}

MU_TEST(test_publish_state_topics) {
    calls_n = 0;
    publisher_t pub = {
        .slug    = "sala",
        .publish = fake_publish,
        .ctx     = NULL,
    };
    int rc = publisher_run_once(&pub);
    mu_assert_int_eq(0, rc);

    mu_check(find_call("ps4/sala/state")           != NULL);
    mu_check(find_call("ps4/sala/cpu/temp")        != NULL);
    mu_check(find_call("ps4/sala/soc/temp")        != NULL);
    mu_check(find_call("ps4/sala/fan/rpm")         != NULL);
    mu_check(find_call("ps4/sala/memory/used_mb")  != NULL);
    mu_check(find_call("ps4/sala/memory/total_mb") != NULL);
    mu_check(find_call("ps4/sala/network/ip")      != NULL);
    mu_check(find_call("ps4/sala/network/ssid")    != NULL);
    mu_check(find_call("ps4/sala/network/rssi")    != NULL);
    mu_check(find_call("ps4/sala/storage/used_gb") != NULL);
    mu_check(find_call("ps4/sala/storage/total_gb")!= NULL);
    mu_check(find_call("ps4/sala/uptime_sec")      != NULL);
    mu_check(find_call("ps4/sala/firmware")        != NULL);
    mu_check(find_call("ps4/sala/game/title")      != NULL);
    mu_check(find_call("ps4/sala/game/title_id")   != NULL);
}

MU_TEST(test_publish_values_match_stub) {
    calls_n = 0;
    publisher_t pub = { .slug = "sala", .publish = fake_publish, .ctx = NULL };
    publisher_run_once(&pub);

    const pub_call_t *c = find_call("ps4/sala/cpu/temp");
    mu_check(c != NULL);
    mu_assert_string_eq("62.5", c->payload);

    c = find_call("ps4/sala/firmware");
    mu_check(c != NULL);
    mu_assert_string_eq("11.00", c->payload);

    c = find_call("ps4/sala/game/title");
    mu_check(c != NULL);
    mu_assert_string_eq("Bloodborne", c->payload);
}

MU_TEST_SUITE(publisher_suite) {
    MU_RUN_TEST(test_publish_state_topics);
    MU_RUN_TEST(test_publish_values_match_stub);
}
