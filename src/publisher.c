#include "publisher.h"
#include "collectors/collectors.h"
#include "log.h"

#include <stdio.h>

static int pub_kv(const publisher_t *p, const char *subtopic,
                  const char *payload) {
    char topic[128];
    snprintf(topic, sizeof(topic), "ps4/%s/%s", p->slug, subtopic);
    return p->publish(p->ctx, topic, payload, /*retain*/ 0);
}

static int pub_int(const publisher_t *p, const char *subtopic, long v) {
    char buf[32];
    snprintf(buf, sizeof(buf), "%ld", v);
    return pub_kv(p, subtopic, buf);
}

static int pub_double(const publisher_t *p, const char *subtopic, double v) {
    char buf[32];
    snprintf(buf, sizeof(buf), "%.1f", v);
    return pub_kv(p, subtopic, buf);
}

int publisher_run_once(const publisher_t *p) {
    if (!p || !p->publish || !p->slug) return -1;

    pub_kv(p, "state", "on");

    system_data_t sys;
    if (collect_system(&sys) == 0) {
        pub_int   (p, "uptime_sec",       (long)sys.uptime_sec);
        pub_int   (p, "memory/used_mb",   (long)sys.mem_used_mb);
        pub_int   (p, "memory/total_mb",  (long)sys.mem_total_mb);
        pub_kv    (p, "firmware",         sys.firmware);
    } else {
        LOG_WARN("publisher: system collector failed");
    }

    thermal_data_t th;
    if (collect_thermal(&th) == 0) {
        pub_double(p, "cpu/temp", th.cpu_temp_c);
        pub_double(p, "soc/temp", th.soc_temp_c);
        pub_int   (p, "fan/rpm",  th.fan_rpm);
    } else {
        LOG_WARN("publisher: thermal collector failed");
    }

    network_data_t net;
    if (collect_network(&net) == 0) {
        pub_kv (p, "network/ip",   net.ip);
        pub_kv (p, "network/ssid", net.ssid);
        pub_int(p, "network/rssi", net.rssi_dbm);
    } else {
        LOG_WARN("publisher: network collector failed");
    }

    storage_data_t st;
    if (collect_storage(&st) == 0) {
        pub_int(p, "storage/used_gb",  (long)st.used_gb);
        pub_int(p, "storage/total_gb", (long)st.total_gb);
    } else {
        LOG_WARN("publisher: storage collector failed");
    }

    app_data_t app;
    if (collect_app(&app) == 0) {
        pub_kv(p, "game/title",    app.in_game ? app.title    : "");
        pub_kv(p, "game/title_id", app.in_game ? app.title_id : "");
    } else {
        LOG_WARN("publisher: app collector failed");
    }

    return 0;
}
