# PS4 MQTT Home Assistant Plugin — Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Build a GoldHEN PRX plugin that polls PS4 telemetry and publishes it to a Home Assistant MQTT broker, auto-creating entities via MQTT Discovery.

**Architecture:** A C plugin compiled with the OpenOrbis PS4 Toolchain. A background pthread polls system/thermal/network/storage/app collectors every 10 seconds and publishes to topics under `ps4/<slug>/...`. A custom minimal MQTT 3.1.1 publisher handles CONNECT/PUBLISH/PINGREQ/DISCONNECT only — no SUBSCRIBE, no QoS 2, no TLS. Last Will and Testament (LWT) keeps Home Assistant availability accurate when the console powers off.

**Tech Stack:** C (C99), OpenOrbis PS4 Toolchain (clang), GoldHEN 2.4b18.3+, libNet (PS4 sockets), libNetCtl (network info), libLNC (current app), cJSON (vendored), minunit (vendored, host tests), Mosquitto (integration test broker).

---

## Reference docs an engineer should read before starting

- MQTT 3.1.1 spec, sections 3.1 (CONNECT), 3.3 (PUBLISH), 3.12 (PINGREQ), 3.14 (DISCONNECT): http://docs.oasis-open.org/mqtt/mqtt/v3.1.1/os/mqtt-v3.1.1-os.html
- Home Assistant MQTT Discovery: https://www.home-assistant.io/integrations/mqtt/#mqtt-discovery
- OpenOrbis PS4 Toolchain README: https://github.com/OpenOrbis/OpenOrbis-PS4-Toolchain
- GoldHEN plugin SDK examples: https://github.com/GoldHEN/GoldHEN_Plugin_SDK
- The design spec at `docs/superpowers/specs/2026-05-07-ps4-mqtt-plugin-design.md`

## Repository layout produced by this plan

```
ps4-mqtt-plugin/
├── Makefile
├── README.md
├── .gitignore
├── config.example.json
├── docs/
│   └── superpowers/
│       ├── specs/2026-05-07-ps4-mqtt-plugin-design.md
│       └── plans/2026-05-07-ps4-mqtt-plugin.md
├── src/
│   ├── main.c
│   ├── log.h
│   ├── log_host.c                  # host-only logger backend
│   ├── log_ps4.c                   # PS4-only logger backend
│   ├── config.h
│   ├── config.c
│   ├── publisher.c
│   ├── publisher.h
│   ├── mqtt/
│   │   ├── mqtt_packet.h
│   │   ├── mqtt_packet.c
│   │   ├── mqtt_socket.h
│   │   ├── mqtt_socket_host.c      # BSD sockets, host build
│   │   ├── mqtt_socket_ps4.c       # libNet, PS4 build
│   │   ├── mqtt_client.h
│   │   └── mqtt_client.c
│   ├── ha/
│   │   ├── ha_discovery.h
│   │   └── ha_discovery.c
│   └── collectors/
│       ├── collectors.h
│       ├── system_ps4.c            # PS4 impl
│       ├── system_host.c           # host stub for tests
│       ├── thermal_ps4.c
│       ├── thermal_host.c
│       ├── network_ps4.c
│       ├── network_host.c
│       ├── storage_ps4.c
│       ├── storage_host.c
│       ├── app_ps4.c
│       └── app_host.c
├── third_party/
│   ├── cJSON/
│   │   ├── cJSON.c
│   │   └── cJSON.h
│   └── minunit/
│       └── minunit.h
└── tests/
    ├── test_config.c
    ├── test_mqtt_packet.c
    ├── test_ha_discovery.c
    ├── test_publisher.c
    └── integration/
        ├── test_mqtt_integration.c
        └── run_integration.sh
```

The split between `*_ps4.c` and `*_host.c` lets pure logic (config, packet, discovery) plus the publisher pipeline run on a host machine for fast TDD, while the PS4-only sources are linked in only when building the PRX.

---

## Phase 1 — Foundation (host-buildable, TDD)

### Task 1: Project scaffolding and host test framework

**Goal:** A `make test` target that compiles a no-op test and runs it. This lets every later task land in TDD form.

**Files:**
- Create: `Makefile`
- Create: `third_party/minunit/minunit.h`
- Create: `tests/test_smoke.c`

- [ ] **Step 1: Vendor minunit**

Download `minunit.h` from https://github.com/siu/minunit/blob/master/minunit.h and save it verbatim to `third_party/minunit/minunit.h`. It is a single MIT-licensed header — do not modify it.

- [ ] **Step 2: Write the smoke test**

`tests/test_smoke.c`:

```c
#include "minunit.h"

MU_TEST(test_truth) {
    mu_check(1 == 1);
}

MU_TEST_SUITE(suite) {
    MU_RUN_TEST(test_truth);
}

int main(void) {
    MU_RUN_SUITE(suite);
    MU_REPORT();
    return MU_EXIT_CODE;
}
```

- [ ] **Step 3: Write the Makefile**

`Makefile`:

```make
# Host build (tests). PS4 build is added in Phase 2.
CC       ?= cc
CFLAGS    = -std=c99 -Wall -Wextra -Wpedantic -O0 -g \
            -Isrc -Ithird_party/minunit -Ithird_party/cJSON
LDFLAGS   =

BUILD_DIR = build
TEST_BIN  = $(BUILD_DIR)/tests

# Test sources are added as more tasks land.
TEST_SOURCES = tests/test_smoke.c

.PHONY: all test clean

all: test

$(BUILD_DIR):
	mkdir -p $(BUILD_DIR)

test: $(BUILD_DIR)
	$(CC) $(CFLAGS) $(TEST_SOURCES) -o $(TEST_BIN) $(LDFLAGS)
	$(TEST_BIN)

clean:
	rm -rf $(BUILD_DIR)
```

- [ ] **Step 4: Run the test**

Run: `make test`
Expected: compiles cleanly, prints `.` and `1 tests, 1 assertions, 0 failures`, exits 0.

- [ ] **Step 5: Commit**

```bash
git add Makefile third_party/minunit/minunit.h tests/test_smoke.c
git commit -m "build: add Makefile and minunit-based smoke test"
```

---

### Task 2: Logger interface with host backend

**Goal:** A logging API the rest of the codebase depends on. Use a real backend on PS4 later; for host tests, write to stderr.

**Files:**
- Create: `src/log.h`
- Create: `src/log_host.c`

- [ ] **Step 1: Write the logger header**

`src/log.h`:

```c
#ifndef PS4MQTT_LOG_H
#define PS4MQTT_LOG_H

typedef enum {
    LOG_LEVEL_ERR   = 0,
    LOG_LEVEL_WARN  = 1,
    LOG_LEVEL_INFO  = 2,
    LOG_LEVEL_DEBUG = 3,
} log_level_t;

void log_init(log_level_t min_level);
void log_write(log_level_t level, const char *fmt, ...);

#define LOG_ERR(...)   log_write(LOG_LEVEL_ERR,   __VA_ARGS__)
#define LOG_WARN(...)  log_write(LOG_LEVEL_WARN,  __VA_ARGS__)
#define LOG_INFO(...)  log_write(LOG_LEVEL_INFO,  __VA_ARGS__)
#define LOG_DEBUG(...) log_write(LOG_LEVEL_DEBUG, __VA_ARGS__)

#endif
```

- [ ] **Step 2: Write the host backend**

`src/log_host.c`:

```c
#include "log.h"

#include <stdarg.h>
#include <stdio.h>

static log_level_t g_min_level = LOG_LEVEL_INFO;

void log_init(log_level_t min_level) {
    g_min_level = min_level;
}

void log_write(log_level_t level, const char *fmt, ...) {
    if (level > g_min_level) return;

    static const char *labels[] = {"ERR", "WARN", "INFO", "DEBUG"};
    fprintf(stderr, "[%s] ", labels[level]);

    va_list args;
    va_start(args, fmt);
    vfprintf(stderr, fmt, args);
    va_end(args);

    fputc('\n', stderr);
}
```

- [ ] **Step 3: Wire into Makefile**

Edit `Makefile`, change:

```make
TEST_SOURCES = tests/test_smoke.c
```

to:

```make
LIB_HOST_SOURCES = \
    src/log_host.c

TEST_SOURCES = tests/test_smoke.c $(LIB_HOST_SOURCES)
```

- [ ] **Step 4: Verify it still builds**

Run: `make clean && make test`
Expected: smoke test still passes; new file compiles without warnings.

- [ ] **Step 5: Commit**

```bash
git add src/log.h src/log_host.c Makefile
git commit -m "feat(log): add logger interface and host backend"
```

---

### Task 3: Vendor cJSON

**Goal:** Drop in a JSON parser used by `config.c` and `ha_discovery.c`.

**Files:**
- Create: `third_party/cJSON/cJSON.h`
- Create: `third_party/cJSON/cJSON.c`

- [ ] **Step 1: Vendor cJSON**

Copy `cJSON.h` and `cJSON.c` verbatim from https://github.com/DaveGamble/cJSON (release v1.7.18 or newer) into `third_party/cJSON/`. Do not modify.

- [ ] **Step 2: Update Makefile**

In `Makefile`, change `LIB_HOST_SOURCES` to:

```make
LIB_HOST_SOURCES = \
    src/log_host.c \
    third_party/cJSON/cJSON.c
```

- [ ] **Step 3: Verify build**

Run: `make clean && make test`
Expected: still passes; cJSON compiles without warnings (cJSON is warning-clean under `-Wall -Wextra`).

- [ ] **Step 4: Commit**

```bash
git add third_party/cJSON/cJSON.h third_party/cJSON/cJSON.c Makefile
git commit -m "build: vendor cJSON v1.7.x"
```

---

### Task 4: Config struct and loader

**Goal:** Read `config.json` into a `config_t`, applying defaults and validating required fields.

**Files:**
- Create: `src/config.h`
- Create: `src/config.c`
- Create: `tests/test_config.c`
- Create: `config.example.json`

- [ ] **Step 1: Write the failing tests**

`tests/test_config.c`:

```c
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
```

- [ ] **Step 2: Add to Makefile and run tests to confirm they fail**

In `Makefile`, change `TEST_SOURCES` to:

```make
TEST_SOURCES = \
    tests/test_smoke.c \
    tests/test_config.c \
    $(LIB_HOST_SOURCES)
```

Run: `make test`
Expected: compile error — `config_t` and `config_load` are undefined.

- [ ] **Step 3: Write the header**

`src/config.h`:

```c
#ifndef PS4MQTT_CONFIG_H
#define PS4MQTT_CONFIG_H

#define CFG_HOST_MAX     64
#define CFG_USER_MAX     32
#define CFG_PASS_MAX     64
#define CFG_DEVNAME_MAX  32

typedef struct {
    char broker_host[CFG_HOST_MAX];
    int  broker_port;
    char username[CFG_USER_MAX];
    char password[CFG_PASS_MAX];
    char device_name[CFG_DEVNAME_MAX];
    int  poll_interval_sec;
} config_t;

/* Returns 0 on success, non-zero on failure. */
int config_load(const char *path, config_t *out);

#endif
```

- [ ] **Step 4: Write the implementation**

`src/config.c`:

```c
#include "config.h"

#include "cJSON.h"
#include "log.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int read_file(const char *path, char **out_buf) {
    FILE *f = fopen(path, "rb");
    if (!f) return -1;
    fseek(f, 0, SEEK_END);
    long len = ftell(f);
    fseek(f, 0, SEEK_SET);
    if (len < 0) { fclose(f); return -1; }
    char *buf = (char *)malloc((size_t)len + 1);
    if (!buf) { fclose(f); return -1; }
    if (fread(buf, 1, (size_t)len, f) != (size_t)len) {
        fclose(f); free(buf); return -1;
    }
    buf[len] = '\0';
    fclose(f);
    *out_buf = buf;
    return 0;
}

static int copy_string_field(const cJSON *root, const char *key,
                             char *dest, size_t dest_len, int required) {
    const cJSON *node = cJSON_GetObjectItemCaseSensitive(root, key);
    if (!cJSON_IsString(node) || node->valuestring == NULL) {
        if (required) {
            LOG_ERR("config: missing required field '%s'", key);
            return -1;
        }
        return 1; /* not present, caller must apply default */
    }
    size_t n = strlen(node->valuestring);
    if (n + 1 > dest_len) {
        LOG_ERR("config: field '%s' too long (max %zu)", key, dest_len - 1);
        return -1;
    }
    memcpy(dest, node->valuestring, n + 1);
    return 0;
}

static int copy_int_field(const cJSON *root, const char *key,
                          int *dest, int required) {
    const cJSON *node = cJSON_GetObjectItemCaseSensitive(root, key);
    if (!cJSON_IsNumber(node)) {
        if (required) {
            LOG_ERR("config: missing required field '%s'", key);
            return -1;
        }
        return 1;
    }
    *dest = node->valueint;
    return 0;
}

int config_load(const char *path, config_t *out) {
    if (!path || !out) return -1;
    memset(out, 0, sizeof(*out));

    char *buf = NULL;
    if (read_file(path, &buf) != 0) {
        LOG_ERR("config: cannot read file '%s'", path);
        return -1;
    }

    cJSON *root = cJSON_Parse(buf);
    free(buf);
    if (!root) {
        LOG_ERR("config: malformed JSON in '%s'", path);
        return -1;
    }

    int rc = 0;
    if (copy_string_field(root, "broker_host", out->broker_host,
                          sizeof(out->broker_host), 1) < 0) { rc = -1; goto done; }
    if (copy_string_field(root, "username", out->username,
                          sizeof(out->username), 1) < 0) { rc = -1; goto done; }
    if (copy_string_field(root, "password", out->password,
                          sizeof(out->password), 1) < 0) { rc = -1; goto done; }

    /* optional fields with defaults */
    out->broker_port = 1883;
    copy_int_field(root, "broker_port", &out->broker_port, 0);

    strncpy(out->device_name, "PS4", sizeof(out->device_name) - 1);
    copy_string_field(root, "device_name", out->device_name,
                      sizeof(out->device_name), 0);

    out->poll_interval_sec = 10;
    copy_int_field(root, "poll_interval_sec", &out->poll_interval_sec, 0);

done:
    cJSON_Delete(root);
    return rc;
}
```

- [ ] **Step 5: Add to Makefile**

In `Makefile`, change `LIB_HOST_SOURCES` to:

```make
LIB_HOST_SOURCES = \
    src/log_host.c \
    src/config.c \
    third_party/cJSON/cJSON.c
```

- [ ] **Step 6: Run tests until they pass**

Run: `make test`
Expected: all five `config_*` tests pass plus `test_truth`.

- [ ] **Step 7: Add a config example file**

`config.example.json`:

```json
{
  "broker_host": "192.168.1.10",
  "broker_port": 1883,
  "username": "ps4",
  "password": "change-me",
  "device_name": "Sala",
  "poll_interval_sec": 10
}
```

- [ ] **Step 8: Commit**

```bash
git add src/config.h src/config.c tests/test_config.c config.example.json Makefile
git commit -m "feat(config): JSON loader with defaults and validation"
```

---

### Task 5: MQTT variable-length integer encoder/decoder

**Goal:** Build the foundation for MQTT 3.1.1 packet encoding. The variable-length "remaining length" field (spec §2.2.3) is used by every packet type.

**Files:**
- Create: `src/mqtt/mqtt_packet.h`
- Create: `src/mqtt/mqtt_packet.c`
- Create: `tests/test_mqtt_packet.c`

- [ ] **Step 1: Write the failing tests**

`tests/test_mqtt_packet.c`:

```c
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
```

- [ ] **Step 2: Add to Makefile and observe failure**

In `Makefile`, change `TEST_SOURCES` to:

```make
TEST_SOURCES = \
    tests/test_smoke.c \
    tests/test_config.c \
    tests/test_mqtt_packet.c \
    $(LIB_HOST_SOURCES)
```

Also change `CFLAGS` to include the mqtt include path:

```make
CFLAGS    = -std=c99 -Wall -Wextra -Wpedantic -O0 -g \
            -Isrc -Isrc/mqtt -Ithird_party/minunit -Ithird_party/cJSON
```

Run: `make test`
Expected: compile error — `mqtt_packet.h` not found.

- [ ] **Step 3: Write the header**

`src/mqtt/mqtt_packet.h`:

```c
#ifndef PS4MQTT_MQTT_PACKET_H
#define PS4MQTT_MQTT_PACKET_H

#include <stddef.h>
#include <stdint.h>

/* MQTT 3.1.1 variable-length integer (spec §2.2.3).
 * Returns bytes written (1..4) on success, or -1 on error
 * (buffer too small or value > 268435455). */
int mqtt_varint_encode(uint8_t *buf, size_t buflen, uint32_t value);

/* Decodes a variable-length integer from buf.
 * Returns bytes consumed on success, or -1 on error. */
int mqtt_varint_decode(const uint8_t *buf, size_t buflen, uint32_t *out);

#endif
```

- [ ] **Step 4: Write the implementation**

`src/mqtt/mqtt_packet.c`:

```c
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
```

- [ ] **Step 5: Wire into Makefile**

In `Makefile`, change `LIB_HOST_SOURCES`:

```make
LIB_HOST_SOURCES = \
    src/log_host.c \
    src/config.c \
    src/mqtt/mqtt_packet.c \
    third_party/cJSON/cJSON.c
```

- [ ] **Step 6: Run tests until they pass**

Run: `make test`
Expected: all varint tests pass.

- [ ] **Step 7: Commit**

```bash
git add src/mqtt/mqtt_packet.h src/mqtt/mqtt_packet.c tests/test_mqtt_packet.c Makefile
git commit -m "feat(mqtt): variable-length integer encode/decode"
```

---

### Task 6: MQTT CONNECT packet encoder

**Goal:** Encode a CONNECT control packet (MQTT 3.1.1 §3.1) with username/password and Last Will support.

**Files:**
- Modify: `src/mqtt/mqtt_packet.h`
- Modify: `src/mqtt/mqtt_packet.c`
- Modify: `tests/test_mqtt_packet.c`

- [ ] **Step 1: Add the failing CONNECT test**

In `tests/test_mqtt_packet.c`, append (above `MU_TEST_SUITE`):

```c
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
```

Then add the new tests to the suite:

```c
MU_RUN_TEST(test_connect_basic_fields);
MU_RUN_TEST(test_connect_buffer_too_small);
```

- [ ] **Step 2: Run tests to confirm failure**

Run: `make test`
Expected: compile error — `mqtt_connect_opts_t`, `mqtt_encode_connect` undefined.

- [ ] **Step 3: Add the API in the header**

Append to `src/mqtt/mqtt_packet.h` (before `#endif`):

```c
typedef struct {
    const char *client_id;       /* required */
    const char *username;        /* NULL to omit */
    const char *password;        /* NULL to omit */
    uint16_t    keepalive_sec;
    int         clean_session;
    const char *will_topic;      /* NULL to omit will */
    const char *will_payload;
    uint8_t     will_qos;
    int         will_retain;
} mqtt_connect_opts_t;

int mqtt_encode_connect(uint8_t *buf, size_t buflen,
                        const mqtt_connect_opts_t *opts);
```

- [ ] **Step 4: Implement in mqtt_packet.c**

Add to `src/mqtt/mqtt_packet.c`:

```c
#include <string.h>

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
```

- [ ] **Step 5: Run tests until they pass**

Run: `make test`
Expected: both new CONNECT tests pass.

- [ ] **Step 6: Commit**

```bash
git add src/mqtt/mqtt_packet.h src/mqtt/mqtt_packet.c tests/test_mqtt_packet.c
git commit -m "feat(mqtt): CONNECT packet encoder with auth and LWT"
```

---

### Task 7: MQTT PUBLISH packet encoder

**Goal:** Encode a PUBLISH packet (MQTT 3.1.1 §3.3) with QoS 0 and retain support.

**Files:**
- Modify: `src/mqtt/mqtt_packet.h`
- Modify: `src/mqtt/mqtt_packet.c`
- Modify: `tests/test_mqtt_packet.c`

- [ ] **Step 1: Write the failing tests**

In `tests/test_mqtt_packet.c`, append:

```c
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
```

Add to suite:

```c
MU_RUN_TEST(test_publish_qos0_no_retain);
MU_RUN_TEST(test_publish_retain_flag);
```

- [ ] **Step 2: Run tests to confirm failure**

Run: `make test`
Expected: compile error — `mqtt_encode_publish` undefined.

- [ ] **Step 3: Add header API**

Append to `src/mqtt/mqtt_packet.h` (before `#endif`):

```c
int mqtt_encode_publish(uint8_t *buf, size_t buflen,
                        const char *topic,
                        const uint8_t *payload, size_t payload_len,
                        uint8_t qos, int retain);
```

- [ ] **Step 4: Implement in mqtt_packet.c**

```c
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
```

- [ ] **Step 5: Run tests until they pass**

Run: `make test`
Expected: PUBLISH tests pass.

- [ ] **Step 6: Commit**

```bash
git add src/mqtt/mqtt_packet.h src/mqtt/mqtt_packet.c tests/test_mqtt_packet.c
git commit -m "feat(mqtt): PUBLISH packet encoder (QoS 0, retain)"
```

---

### Task 8: MQTT PINGREQ, DISCONNECT encoders and CONNACK/PINGRESP parser

**Goal:** Round out the packet API: short control packets and an ACK parser.

**Files:**
- Modify: `src/mqtt/mqtt_packet.h`
- Modify: `src/mqtt/mqtt_packet.c`
- Modify: `tests/test_mqtt_packet.c`

- [ ] **Step 1: Write the failing tests**

In `tests/test_mqtt_packet.c`, append:

```c
MU_TEST(test_pingreq_bytes) {
    uint8_t buf[4];
    int n = mqtt_encode_pingreq(buf, sizeof(buf));
    mu_assert_int_eq(2, n);
    mu_assert_int_eq(0xC0, buf[0]);
    mu_assert_int_eq(0x00, buf[1]);
}

MU_TEST(test_disconnect_bytes) {
    uint8_t buf[4];
    int n = mqtt_encode_disconnect(buf, sizeof(buf));
    mu_assert_int_eq(2, n);
    mu_assert_int_eq(0xE0, buf[0]);
    mu_assert_int_eq(0x00, buf[1]);
}

MU_TEST(test_parse_connack_accepted) {
    uint8_t pkt[] = {0x20, 0x02, 0x00, 0x00};
    uint8_t return_code = 0xFF;
    int rc = mqtt_parse_connack(pkt, sizeof(pkt), &return_code);
    mu_assert_int_eq(0, rc);
    mu_assert_int_eq(0x00, return_code);
}

MU_TEST(test_parse_connack_rejected) {
    uint8_t pkt[] = {0x20, 0x02, 0x00, 0x05}; /* not authorized */
    uint8_t return_code = 0xFF;
    int rc = mqtt_parse_connack(pkt, sizeof(pkt), &return_code);
    mu_assert_int_eq(0, rc);
    mu_assert_int_eq(0x05, return_code);
}

MU_TEST(test_parse_connack_wrong_type) {
    uint8_t pkt[] = {0x30, 0x02, 0x00, 0x00};
    uint8_t return_code = 0;
    int rc = mqtt_parse_connack(pkt, sizeof(pkt), &return_code);
    mu_check(rc != 0);
}

MU_TEST(test_is_pingresp) {
    uint8_t ok[]   = {0xD0, 0x00};
    uint8_t bad1[] = {0xC0, 0x00};
    uint8_t bad2[] = {0xD0};
    mu_check(mqtt_is_pingresp(ok, sizeof(ok)) == 1);
    mu_check(mqtt_is_pingresp(bad1, sizeof(bad1)) == 0);
    mu_check(mqtt_is_pingresp(bad2, sizeof(bad2)) == 0);
}
```

Add to suite:

```c
MU_RUN_TEST(test_pingreq_bytes);
MU_RUN_TEST(test_disconnect_bytes);
MU_RUN_TEST(test_parse_connack_accepted);
MU_RUN_TEST(test_parse_connack_rejected);
MU_RUN_TEST(test_parse_connack_wrong_type);
MU_RUN_TEST(test_is_pingresp);
```

- [ ] **Step 2: Run tests to confirm failure**

Run: `make test`
Expected: compile error — five new symbols undefined.

- [ ] **Step 3: Add header API**

Append to `src/mqtt/mqtt_packet.h`:

```c
int mqtt_encode_pingreq(uint8_t *buf, size_t buflen);
int mqtt_encode_disconnect(uint8_t *buf, size_t buflen);

/* Returns 0 on success and sets *return_code; non-zero on parse failure. */
int mqtt_parse_connack(const uint8_t *buf, size_t buflen, uint8_t *return_code);

/* Returns 1 if the bytes match a PINGRESP packet, 0 otherwise. */
int mqtt_is_pingresp(const uint8_t *buf, size_t buflen);
```

- [ ] **Step 4: Implement**

Append to `src/mqtt/mqtt_packet.c`:

```c
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
```

- [ ] **Step 5: Run tests until they pass**

Run: `make test`
Expected: all six new tests pass.

- [ ] **Step 6: Commit**

```bash
git add src/mqtt/mqtt_packet.h src/mqtt/mqtt_packet.c tests/test_mqtt_packet.c
git commit -m "feat(mqtt): PINGREQ/DISCONNECT encoders and CONNACK parser"
```

---

### Task 9: Socket abstraction interface and host BSD backend

**Goal:** Define a tiny socket API the MQTT client uses. Implement it with BSD sockets on the host so we can run integration tests offline.

**Files:**
- Create: `src/mqtt/mqtt_socket.h`
- Create: `src/mqtt/mqtt_socket_host.c`

- [ ] **Step 1: Write the header**

`src/mqtt/mqtt_socket.h`:

```c
#ifndef PS4MQTT_MQTT_SOCKET_H
#define PS4MQTT_MQTT_SOCKET_H

#include <stddef.h>

/* Returns a non-negative socket fd on success, -1 on failure. */
int mqtt_socket_connect(const char *host, int port);

/* Returns bytes sent (== len) on success; -1 on failure. */
int mqtt_socket_send(int fd, const void *buf, size_t len);

/* Receives up to len bytes, blocking up to timeout_ms (-1 = block forever).
 * Returns bytes read (>0), 0 on timeout, -1 on error/disconnect. */
int mqtt_socket_recv(int fd, void *buf, size_t len, int timeout_ms);

void mqtt_socket_close(int fd);

#endif
```

- [ ] **Step 2: Write the host backend**

`src/mqtt/mqtt_socket_host.c`:

```c
#include "mqtt_socket.h"

#include <arpa/inet.h>
#include <errno.h>
#include <netdb.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/select.h>
#include <unistd.h>

int mqtt_socket_connect(const char *host, int port) {
    struct addrinfo hints, *res = NULL;
    char port_str[8];
    snprintf(port_str, sizeof(port_str), "%d", port);
    memset(&hints, 0, sizeof(hints));
    hints.ai_family = AF_INET;
    hints.ai_socktype = SOCK_STREAM;
    if (getaddrinfo(host, port_str, &hints, &res) != 0 || !res) return -1;

    int fd = socket(res->ai_family, res->ai_socktype, res->ai_protocol);
    if (fd < 0) { freeaddrinfo(res); return -1; }

    if (connect(fd, res->ai_addr, res->ai_addrlen) != 0) {
        close(fd);
        freeaddrinfo(res);
        return -1;
    }
    freeaddrinfo(res);

    int yes = 1;
    setsockopt(fd, IPPROTO_TCP, TCP_NODELAY, &yes, sizeof(yes));
    return fd;
}

int mqtt_socket_send(int fd, const void *buf, size_t len) {
    const char *p = (const char *)buf;
    size_t sent = 0;
    while (sent < len) {
        ssize_t n = send(fd, p + sent, len - sent, 0);
        if (n < 0) {
            if (errno == EINTR) continue;
            return -1;
        }
        sent += (size_t)n;
    }
    return (int)sent;
}

int mqtt_socket_recv(int fd, void *buf, size_t len, int timeout_ms) {
    if (timeout_ms >= 0) {
        fd_set rfds;
        FD_ZERO(&rfds);
        FD_SET(fd, &rfds);
        struct timeval tv = {
            .tv_sec  = timeout_ms / 1000,
            .tv_usec = (timeout_ms % 1000) * 1000,
        };
        int sel = select(fd + 1, &rfds, NULL, NULL, &tv);
        if (sel == 0) return 0;
        if (sel < 0)  return -1;
    }
    ssize_t n = recv(fd, buf, len, 0);
    if (n <= 0) return n == 0 ? -1 : -1;
    return (int)n;
}

void mqtt_socket_close(int fd) {
    if (fd >= 0) close(fd);
}
```

- [ ] **Step 3: Wire into Makefile**

In `Makefile`, change `LIB_HOST_SOURCES`:

```make
LIB_HOST_SOURCES = \
    src/log_host.c \
    src/config.c \
    src/mqtt/mqtt_packet.c \
    src/mqtt/mqtt_socket_host.c \
    third_party/cJSON/cJSON.c
```

- [ ] **Step 4: Verify build**

Run: `make clean && make test`
Expected: still passes; new file compiles cleanly.

- [ ] **Step 5: Commit**

```bash
git add src/mqtt/mqtt_socket.h src/mqtt/mqtt_socket_host.c Makefile
git commit -m "feat(mqtt): socket abstraction with host BSD backend"
```

---

### Task 10: MQTT client connect, publish and graceful disconnect

**Goal:** Build a stateful MQTT client around the packet and socket primitives. Verify against a real Mosquitto broker.

**Files:**
- Create: `src/mqtt/mqtt_client.h`
- Create: `src/mqtt/mqtt_client.c`
- Create: `tests/integration/test_mqtt_integration.c`
- Create: `tests/integration/run_integration.sh`

- [ ] **Step 1: Write the integration test**

`tests/integration/test_mqtt_integration.c`:

```c
#include "minunit.h"
#include "../../src/mqtt/mqtt_client.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

/* Requires a local Mosquitto on 127.0.0.1:1883 with anonymous publish
 * allowed for the test_user. The wrapper run_integration.sh sets this up. */

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
```

- [ ] **Step 2: Write the integration runner script**

`tests/integration/run_integration.sh`:

```bash
#!/usr/bin/env bash
# Spins up a temporary mosquitto broker on :1883 with auth, runs the
# integration test binary, then tears the broker down.
set -euo pipefail

if ! command -v mosquitto >/dev/null; then
    echo "mosquitto not installed (brew install mosquitto / apt install mosquitto)" >&2
    exit 1
fi

WORKDIR="$(mktemp -d)"
trap 'rm -rf "$WORKDIR"' EXIT

PWFILE="$WORKDIR/passwd"
CONF="$WORKDIR/mosquitto.conf"
LOGF="$WORKDIR/mosquitto.log"

mosquitto_passwd -c -b "$PWFILE" test_user test_pass >/dev/null 2>&1

cat > "$CONF" <<EOF
listener 1883 127.0.0.1
allow_anonymous false
password_file $PWFILE
log_dest file $LOGF
EOF

mosquitto -c "$CONF" -d
BROKER_PID="$(pgrep -f "mosquitto -c $CONF" | head -1)"
trap 'kill "$BROKER_PID" 2>/dev/null || true; rm -rf "$WORKDIR"' EXIT

# Wait for broker to bind.
for _ in $(seq 1 20); do
    if (echo > /dev/tcp/127.0.0.1/1883) 2>/dev/null; then break; fi
    sleep 0.1
done

"./build/integration"
```

Make it executable:

```bash
chmod +x tests/integration/run_integration.sh
```

- [ ] **Step 3: Add the integration target to Makefile**

In `Makefile`, append:

```make
INTEGRATION_BIN     = $(BUILD_DIR)/integration
INTEGRATION_SOURCES = tests/integration/test_mqtt_integration.c \
                      $(LIB_HOST_SOURCES) \
                      src/mqtt/mqtt_client.c

.PHONY: integration

integration: $(BUILD_DIR)
	$(CC) $(CFLAGS) $(INTEGRATION_SOURCES) -o $(INTEGRATION_BIN) $(LDFLAGS)
	./tests/integration/run_integration.sh
```

- [ ] **Step 4: Run the integration target — confirm it fails**

Run: `make integration`
Expected: compile error — `mqtt_client_*` symbols undefined.

- [ ] **Step 5: Write the client header**

`src/mqtt/mqtt_client.h`:

```c
#ifndef PS4MQTT_MQTT_CLIENT_H
#define PS4MQTT_MQTT_CLIENT_H

typedef struct mqtt_client mqtt_client_t;

mqtt_client_t *mqtt_client_new(const char *host, int port,
                               const char *user, const char *pass,
                               const char *client_id,
                               int keepalive_sec,
                               const char *will_topic,
                               const char *will_payload);

int  mqtt_client_connect(mqtt_client_t *c);
int  mqtt_client_publish(mqtt_client_t *c, const char *topic,
                         const char *payload, int retain);
int  mqtt_client_ping(mqtt_client_t *c);
int  mqtt_client_disconnect(mqtt_client_t *c);
int  mqtt_client_is_connected(const mqtt_client_t *c);
void mqtt_client_free(mqtt_client_t *c);

#endif
```

- [ ] **Step 6: Write the client implementation**

`src/mqtt/mqtt_client.c`:

```c
#include "mqtt_client.h"
#include "mqtt_packet.h"
#include "mqtt_socket.h"
#include "../log.h"

#include <stdlib.h>
#include <string.h>

#define BUF_MAX 1024

struct mqtt_client {
    char host[64];
    int  port;
    char user[32];
    char pass[64];
    char client_id[64];
    int  keepalive_sec;
    char will_topic[96];
    char will_payload[64];
    int  fd;
    int  connected;
};

static void copy_or_empty(char *dst, size_t cap, const char *src) {
    if (!src) { dst[0] = '\0'; return; }
    strncpy(dst, src, cap - 1);
    dst[cap - 1] = '\0';
}

mqtt_client_t *mqtt_client_new(const char *host, int port,
                               const char *user, const char *pass,
                               const char *client_id,
                               int keepalive_sec,
                               const char *will_topic,
                               const char *will_payload) {
    if (!host || !client_id) return NULL;
    mqtt_client_t *c = calloc(1, sizeof(*c));
    if (!c) return NULL;
    copy_or_empty(c->host,         sizeof(c->host),         host);
    c->port = port;
    copy_or_empty(c->user,         sizeof(c->user),         user);
    copy_or_empty(c->pass,         sizeof(c->pass),         pass);
    copy_or_empty(c->client_id,    sizeof(c->client_id),    client_id);
    c->keepalive_sec = keepalive_sec;
    copy_or_empty(c->will_topic,   sizeof(c->will_topic),   will_topic);
    copy_or_empty(c->will_payload, sizeof(c->will_payload), will_payload);
    c->fd = -1;
    return c;
}

int mqtt_client_connect(mqtt_client_t *c) {
    if (!c) return -1;
    c->fd = mqtt_socket_connect(c->host, c->port);
    if (c->fd < 0) {
        LOG_WARN("mqtt: socket connect failed to %s:%d", c->host, c->port);
        return -1;
    }

    uint8_t buf[BUF_MAX];
    mqtt_connect_opts_t opts = {
        .client_id     = c->client_id,
        .username      = c->user[0] ? c->user : NULL,
        .password      = c->pass[0] ? c->pass : NULL,
        .keepalive_sec = (uint16_t)c->keepalive_sec,
        .clean_session = 1,
    };
    if (c->will_topic[0]) {
        opts.will_topic   = c->will_topic;
        opts.will_payload = c->will_payload;
        opts.will_qos     = 1;
        opts.will_retain  = 1;
    }
    int n = mqtt_encode_connect(buf, sizeof(buf), &opts);
    if (n <= 0) goto fail;
    if (mqtt_socket_send(c->fd, buf, (size_t)n) != n) goto fail;

    /* Wait up to 5 s for CONNACK. */
    int got = mqtt_socket_recv(c->fd, buf, sizeof(buf), 5000);
    if (got <= 0) {
        LOG_WARN("mqtt: no CONNACK within 5 s");
        goto fail;
    }
    uint8_t rc = 0xFF;
    if (mqtt_parse_connack(buf, (size_t)got, &rc) != 0) {
        LOG_WARN("mqtt: malformed CONNACK");
        goto fail;
    }
    if (rc != 0) {
        LOG_WARN("mqtt: broker rejected connection (rc=0x%02X)", rc);
        goto fail;
    }
    c->connected = 1;
    LOG_INFO("mqtt: connected to %s:%d as %s", c->host, c->port, c->client_id);
    return 0;

fail:
    mqtt_socket_close(c->fd);
    c->fd = -1;
    c->connected = 0;
    return -1;
}

int mqtt_client_publish(mqtt_client_t *c, const char *topic,
                        const char *payload, int retain) {
    if (!c || !c->connected) return -1;
    uint8_t buf[BUF_MAX];
    size_t plen = payload ? strlen(payload) : 0;
    int n = mqtt_encode_publish(buf, sizeof(buf), topic,
                                (const uint8_t *)payload, plen,
                                /*qos*/ 0, retain);
    if (n <= 0) return -1;
    if (mqtt_socket_send(c->fd, buf, (size_t)n) != n) {
        LOG_WARN("mqtt: publish send failed");
        c->connected = 0;
        return -1;
    }
    return 0;
}

int mqtt_client_ping(mqtt_client_t *c) {
    if (!c || !c->connected) return -1;
    uint8_t buf[2];
    int n = mqtt_encode_pingreq(buf, sizeof(buf));
    if (mqtt_socket_send(c->fd, buf, (size_t)n) != n) return -1;
    int got = mqtt_socket_recv(c->fd, buf, sizeof(buf), 3000);
    if (got <= 0 || !mqtt_is_pingresp(buf, (size_t)got)) {
        c->connected = 0;
        return -1;
    }
    return 0;
}

int mqtt_client_disconnect(mqtt_client_t *c) {
    if (!c) return -1;
    if (c->connected) {
        uint8_t buf[2];
        int n = mqtt_encode_disconnect(buf, sizeof(buf));
        mqtt_socket_send(c->fd, buf, (size_t)n);
    }
    if (c->fd >= 0) mqtt_socket_close(c->fd);
    c->fd = -1;
    c->connected = 0;
    return 0;
}

int mqtt_client_is_connected(const mqtt_client_t *c) {
    return c ? c->connected : 0;
}

void mqtt_client_free(mqtt_client_t *c) {
    if (!c) return;
    mqtt_client_disconnect(c);
    free(c);
}
```

- [ ] **Step 7: Run the integration test until it passes**

Run: `make integration`
Expected: mosquitto starts, the test connects, publishes, disconnects, exits 0.

- [ ] **Step 8: Commit**

```bash
git add src/mqtt/mqtt_client.h src/mqtt/mqtt_client.c \
        tests/integration/test_mqtt_integration.c \
        tests/integration/run_integration.sh \
        Makefile
git commit -m "feat(mqtt): client connect/publish/disconnect with integration test"
```

---

### Task 11: HA Discovery — slug helper and base sensor builder

**Goal:** Generate the JSON config payload Home Assistant expects so a sensor entity is auto-created.

**Files:**
- Create: `src/ha/ha_discovery.h`
- Create: `src/ha/ha_discovery.c`
- Create: `tests/test_ha_discovery.c`

- [ ] **Step 1: Write the failing tests**

`tests/test_ha_discovery.c`:

```c
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
                                    /*unit*/ "°C",
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
```

- [ ] **Step 2: Add to Makefile and confirm failure**

In `Makefile`, change `TEST_SOURCES`:

```make
TEST_SOURCES = \
    tests/test_smoke.c \
    tests/test_config.c \
    tests/test_mqtt_packet.c \
    tests/test_ha_discovery.c \
    $(LIB_HOST_SOURCES)
```

Add `-Isrc/ha` to `CFLAGS`:

```make
CFLAGS    = -std=c99 -Wall -Wextra -Wpedantic -O0 -g \
            -Isrc -Isrc/mqtt -Isrc/ha -Ithird_party/minunit -Ithird_party/cJSON
```

Run: `make test`
Expected: compile error — `ha_*` symbols undefined.

- [ ] **Step 3: Write the header**

`src/ha/ha_discovery.h`:

```c
#ifndef PS4MQTT_HA_DISCOVERY_H
#define PS4MQTT_HA_DISCOVERY_H

#include <stddef.h>

typedef struct {
    const char *slug;   /* e.g. "sala"                 */
    const char *name;   /* e.g. "PS4 Sala"             */
    const char *fw;     /* e.g. "11.00"                */
} ha_device_t;

int ha_make_slug(const char *display_name, char *out, size_t out_len);

int ha_build_sensor_config(char *out, size_t out_len,
                           const ha_device_t *dev,
                           const char *key,
                           const char *friendly_name,
                           const char *state_subtopic,
                           const char *unit,
                           const char *device_class,
                           const char *state_class);

int ha_build_binary_sensor_config(char *out, size_t out_len,
                                  const ha_device_t *dev,
                                  const char *key,
                                  const char *friendly_name,
                                  const char *state_subtopic,
                                  const char *payload_on,
                                  const char *payload_off);

#endif
```

- [ ] **Step 4: Implement**

`src/ha/ha_discovery.c`:

```c
#include "ha_discovery.h"

#include <ctype.h>
#include <stdio.h>
#include <string.h>

int ha_make_slug(const char *display_name, char *out, size_t out_len) {
    if (!display_name || !out || out_len == 0) return -1;
    size_t pos = 0;
    int last_was_underscore = 1;
    for (const char *p = display_name; *p; ++p) {
        unsigned char c = (unsigned char)*p;
        if (isalnum(c)) {
            if (pos + 1 >= out_len) return -1;
            out[pos++] = (char)tolower(c);
            last_was_underscore = 0;
        } else {
            if (!last_was_underscore && pos + 1 < out_len) {
                out[pos++] = '_';
                last_was_underscore = 1;
            }
        }
    }
    /* trim trailing underscore */
    while (pos > 0 && out[pos - 1] == '_') --pos;
    if (pos == 0) return -1;
    if (pos >= out_len) return -1;
    out[pos] = '\0';
    return 0;
}

int ha_build_sensor_config(char *out, size_t out_len,
                           const ha_device_t *dev,
                           const char *key,
                           const char *friendly_name,
                           const char *state_subtopic,
                           const char *unit,
                           const char *device_class,
                           const char *state_class) {
    if (!out || !dev || !key) return -1;
    int n = snprintf(out, out_len,
        "{"
          "\"name\":\"%s\","
          "\"unique_id\":\"ps4_%s_%s\","
          "\"state_topic\":\"ps4/%s/%s\","
          "\"availability_topic\":\"ps4/%s/availability\","
          "\"unit_of_measurement\":\"%s\","
          "\"device_class\":\"%s\","
          "\"state_class\":\"%s\","
          "\"device\":{"
            "\"identifiers\":[\"ps4_%s\"],"
            "\"name\":\"%s\","
            "\"manufacturer\":\"Sony\","
            "\"model\":\"PlayStation 4\","
            "\"sw_version\":\"%s\""
          "}"
        "}",
        friendly_name,
        dev->slug, key,
        dev->slug, state_subtopic,
        dev->slug,
        unit ? unit : "",
        device_class ? device_class : "",
        state_class ? state_class : "",
        dev->slug,
        dev->name,
        dev->fw ? dev->fw : "");
    if (n < 0 || (size_t)n >= out_len) return -1;
    return 0;
}

int ha_build_binary_sensor_config(char *out, size_t out_len,
                                  const ha_device_t *dev,
                                  const char *key,
                                  const char *friendly_name,
                                  const char *state_subtopic,
                                  const char *payload_on,
                                  const char *payload_off) {
    if (!out || !dev || !key) return -1;
    int n = snprintf(out, out_len,
        "{"
          "\"name\":\"%s\","
          "\"unique_id\":\"ps4_%s_%s\","
          "\"state_topic\":\"ps4/%s/%s\","
          "\"availability_topic\":\"ps4/%s/availability\","
          "\"payload_on\":\"%s\","
          "\"payload_off\":\"%s\","
          "\"device\":{"
            "\"identifiers\":[\"ps4_%s\"],"
            "\"name\":\"%s\","
            "\"manufacturer\":\"Sony\","
            "\"model\":\"PlayStation 4\","
            "\"sw_version\":\"%s\""
          "}"
        "}",
        friendly_name,
        dev->slug, key,
        dev->slug, state_subtopic,
        dev->slug,
        payload_on, payload_off,
        dev->slug,
        dev->name,
        dev->fw ? dev->fw : "");
    if (n < 0 || (size_t)n >= out_len) return -1;
    return 0;
}
```

- [ ] **Step 5: Wire into Makefile**

In `Makefile`, change `LIB_HOST_SOURCES`:

```make
LIB_HOST_SOURCES = \
    src/log_host.c \
    src/config.c \
    src/mqtt/mqtt_packet.c \
    src/mqtt/mqtt_socket_host.c \
    src/ha/ha_discovery.c \
    third_party/cJSON/cJSON.c
```

- [ ] **Step 6: Run tests until they pass**

Run: `make test`
Expected: four new HA tests pass.

- [ ] **Step 7: Commit**

```bash
git add src/ha/ha_discovery.h src/ha/ha_discovery.c \
        tests/test_ha_discovery.c Makefile
git commit -m "feat(ha): MQTT Discovery slug + sensor/binary_sensor builders"
```

---

### Task 12: Collector data structs and host stubs

**Goal:** Define the structs each collector returns, plus host stubs that return canned data so the publisher pipeline can be tested without a PS4.

**Files:**
- Create: `src/collectors/collectors.h`
- Create: `src/collectors/system_host.c`
- Create: `src/collectors/thermal_host.c`
- Create: `src/collectors/network_host.c`
- Create: `src/collectors/storage_host.c`
- Create: `src/collectors/app_host.c`

- [ ] **Step 1: Write the collector header**

`src/collectors/collectors.h`:

```c
#ifndef PS4MQTT_COLLECTORS_H
#define PS4MQTT_COLLECTORS_H

#include <stdint.h>

typedef struct {
    int64_t uptime_sec;
    uint64_t mem_used_mb;
    uint64_t mem_total_mb;
    char firmware[16];
} system_data_t;

typedef struct {
    double cpu_temp_c;
    double soc_temp_c;
    int    fan_rpm;
} thermal_data_t;

typedef struct {
    char ip[16];
    char ssid[33];
    int  rssi_dbm;
} network_data_t;

typedef struct {
    uint64_t used_gb;
    uint64_t total_gb;
} storage_data_t;

typedef struct {
    int  in_game;            /* 1 if a game is running, 0 otherwise */
    char title[64];          /* "" when not in game */
    char title_id[16];       /* "" when not in game */
} app_data_t;

int collect_system (system_data_t  *out);
int collect_thermal(thermal_data_t *out);
int collect_network(network_data_t *out);
int collect_storage(storage_data_t *out);
int collect_app    (app_data_t     *out);

#endif
```

- [ ] **Step 2: Write all five host stubs**

`src/collectors/system_host.c`:

```c
#include "collectors.h"

#include <string.h>

int collect_system(system_data_t *out) {
    if (!out) return -1;
    out->uptime_sec   = 12345;
    out->mem_used_mb  = 4096;
    out->mem_total_mb = 8192;
    strncpy(out->firmware, "11.00", sizeof(out->firmware) - 1);
    out->firmware[sizeof(out->firmware) - 1] = '\0';
    return 0;
}
```

`src/collectors/thermal_host.c`:

```c
#include "collectors.h"

int collect_thermal(thermal_data_t *out) {
    if (!out) return -1;
    out->cpu_temp_c = 62.5;
    out->soc_temp_c = 58.0;
    out->fan_rpm    = 1800;
    return 0;
}
```

`src/collectors/network_host.c`:

```c
#include "collectors.h"

#include <string.h>

int collect_network(network_data_t *out) {
    if (!out) return -1;
    strncpy(out->ip,   "192.168.1.50", sizeof(out->ip)   - 1);
    strncpy(out->ssid, "MinhaRede",    sizeof(out->ssid) - 1);
    out->ip[sizeof(out->ip)   - 1] = '\0';
    out->ssid[sizeof(out->ssid) - 1] = '\0';
    out->rssi_dbm = -55;
    return 0;
}
```

`src/collectors/storage_host.c`:

```c
#include "collectors.h"

int collect_storage(storage_data_t *out) {
    if (!out) return -1;
    out->used_gb  = 320;
    out->total_gb = 500;
    return 0;
}
```

`src/collectors/app_host.c`:

```c
#include "collectors.h"

#include <string.h>

int collect_app(app_data_t *out) {
    if (!out) return -1;
    out->in_game = 1;
    strncpy(out->title,    "Bloodborne", sizeof(out->title)    - 1);
    strncpy(out->title_id, "CUSA00900",  sizeof(out->title_id) - 1);
    out->title[sizeof(out->title)       - 1] = '\0';
    out->title_id[sizeof(out->title_id) - 1] = '\0';
    return 0;
}
```

- [ ] **Step 3: Wire into Makefile**

In `Makefile`, change `LIB_HOST_SOURCES`:

```make
LIB_HOST_SOURCES = \
    src/log_host.c \
    src/config.c \
    src/mqtt/mqtt_packet.c \
    src/mqtt/mqtt_socket_host.c \
    src/ha/ha_discovery.c \
    src/collectors/system_host.c \
    src/collectors/thermal_host.c \
    src/collectors/network_host.c \
    src/collectors/storage_host.c \
    src/collectors/app_host.c \
    third_party/cJSON/cJSON.c
```

Add `-Isrc/collectors` to `CFLAGS`:

```make
CFLAGS    = -std=c99 -Wall -Wextra -Wpedantic -O0 -g \
            -Isrc -Isrc/mqtt -Isrc/ha -Isrc/collectors \
            -Ithird_party/minunit -Ithird_party/cJSON
```

- [ ] **Step 4: Verify build**

Run: `make clean && make test`
Expected: still passes; new files compile cleanly.

- [ ] **Step 5: Commit**

```bash
git add src/collectors/ Makefile
git commit -m "feat(collectors): data structs and host stubs"
```

---

### Task 13: Publisher orchestrator

**Goal:** A `publisher` module that, given a connected client and a config, runs one full cycle: collect everything, publish all topics. Tested with the host stub collectors.

**Files:**
- Create: `src/publisher.h`
- Create: `src/publisher.c`
- Create: `tests/test_publisher.c`

- [ ] **Step 1: Write the failing tests**

`tests/test_publisher.c`:

```c
#include "minunit.h"
#include "../src/publisher.h"
#include "../src/collectors/collectors.h"

#include <string.h>

/* The test uses a fake mqtt_publish_fn that records calls. */
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
```

- [ ] **Step 2: Add to Makefile and confirm failure**

In `Makefile`, change `TEST_SOURCES`:

```make
TEST_SOURCES = \
    tests/test_smoke.c \
    tests/test_config.c \
    tests/test_mqtt_packet.c \
    tests/test_ha_discovery.c \
    tests/test_publisher.c \
    $(LIB_HOST_SOURCES)
```

Run: `make test`
Expected: compile error — `publisher_t`, `publisher_run_once` undefined.

- [ ] **Step 3: Write the header**

`src/publisher.h`:

```c
#ifndef PS4MQTT_PUBLISHER_H
#define PS4MQTT_PUBLISHER_H

typedef int (*mqtt_publish_fn)(void *ctx, const char *topic,
                               const char *payload, int retain);

typedef struct {
    const char     *slug;     /* e.g. "sala" */
    mqtt_publish_fn publish;
    void           *ctx;      /* opaque, passed to publish */
} publisher_t;

int publisher_run_once(const publisher_t *pub);

#endif
```

- [ ] **Step 4: Write the implementation**

`src/publisher.c`:

```c
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

    /* state — for MVP we always say "on" while plugin runs.
       LWT handles "offline"; standby detection is out of scope. */
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
```

- [ ] **Step 5: Wire into Makefile**

In `Makefile`, change `LIB_HOST_SOURCES`:

```make
LIB_HOST_SOURCES = \
    src/log_host.c \
    src/config.c \
    src/publisher.c \
    src/mqtt/mqtt_packet.c \
    src/mqtt/mqtt_socket_host.c \
    src/ha/ha_discovery.c \
    src/collectors/system_host.c \
    src/collectors/thermal_host.c \
    src/collectors/network_host.c \
    src/collectors/storage_host.c \
    src/collectors/app_host.c \
    third_party/cJSON/cJSON.c
```

- [ ] **Step 6: Run tests until they pass**

Run: `make test`
Expected: both publisher tests pass.

- [ ] **Step 7: Commit**

```bash
git add src/publisher.h src/publisher.c tests/test_publisher.c Makefile
git commit -m "feat(publisher): orchestrate collectors and publish per-topic values"
```

---

## Phase 2 — PS4 build and real telemetry

Phase 2 requires the OpenOrbis PS4 Toolchain installed and a PS4 with GoldHEN 2.4b18.3+ for smoke testing. It compiles the PRX, replaces the host stubs with real data sources, and wires up the plugin lifecycle.

The engineer should install OpenOrbis following https://github.com/OpenOrbis/OpenOrbis-PS4-Toolchain, set `OO_PS4_TOOLCHAIN` env var, and confirm `$OO_PS4_TOOLCHAIN/bin/clang` is on `PATH` before starting Phase 2. They should also clone `GoldHEN_Plugin_SDK` and read `samples/sample_plugin/` to learn the plugin entrypoint shape used here.

### Task 14: PS4 PRX build target

**Goal:** Add a `make prx` target that produces `build/ps4-mqtt.prx` using OpenOrbis. No new behavior — just the build wiring.

**Files:**
- Modify: `Makefile`

- [ ] **Step 1: Append the PRX build target to Makefile**

Add at the bottom of `Makefile`:

```make
# ----- PS4 PRX build (Phase 2) ---------------------------------------------
# Requires OpenOrbis toolchain. Set OO_PS4_TOOLCHAIN to its install root.

OO          ?= $(OO_PS4_TOOLCHAIN)
PS4_CC       = $(OO)/bin/clang
PS4_LD       = $(OO)/bin/ld.lld
PS4_CREATE   = $(OO)/bin/create-fself

PS4_CFLAGS   = -cc1 -triple x86_64-pc-freebsd \
               -munwind-tables -fuse-init-array \
               -isysroot $(OO) -isystem $(OO)/include \
               -isystem $(OO)/include/c++/v1 \
               -O2 -fPIC -std=c99 \
               -Isrc -Isrc/mqtt -Isrc/ha -Isrc/collectors \
               -Ithird_party/cJSON
PS4_LDFLAGS  = -m elf_x86_64 --eh-frame-hdr --oformat=ELF \
               -pie -L$(OO)/lib \
               -lc -lkernel -lc++ -lScePosix \
               -lSceLibcInternal -lSceNet -lSceNetCtl -lSceSystemService

PS4_SOURCES  = \
    src/main.c \
    src/log_ps4.c \
    src/config.c \
    src/publisher.c \
    src/mqtt/mqtt_packet.c \
    src/mqtt/mqtt_socket_ps4.c \
    src/mqtt/mqtt_client.c \
    src/ha/ha_discovery.c \
    src/collectors/system_ps4.c \
    src/collectors/thermal_ps4.c \
    src/collectors/network_ps4.c \
    src/collectors/storage_ps4.c \
    src/collectors/app_ps4.c \
    third_party/cJSON/cJSON.c

PS4_OBJ_DIR  = $(BUILD_DIR)/ps4
PS4_OBJS     = $(PS4_SOURCES:%.c=$(PS4_OBJ_DIR)/%.o)
PS4_ELF      = $(BUILD_DIR)/ps4-mqtt.elf
PS4_PRX      = $(BUILD_DIR)/ps4-mqtt.prx

.PHONY: prx prx-clean

$(PS4_OBJ_DIR)/%.o: %.c
	@mkdir -p $(dir $@)
	$(PS4_CC) $(PS4_CFLAGS) -emit-obj $< -o $@

$(PS4_ELF): $(PS4_OBJS)
	$(PS4_LD) $(PS4_LDFLAGS) -o $@ $^

$(PS4_PRX): $(PS4_ELF)
	$(PS4_CREATE) -in=$(PS4_ELF) --out=$(PS4_PRX) --paid 0x3800000000000011

prx: $(PS4_PRX)

prx-clean:
	rm -rf $(PS4_OBJ_DIR) $(PS4_ELF) $(PS4_PRX)
```

- [ ] **Step 2: Confirm the host build still works**

Run: `make clean && make test`
Expected: still passes — Phase 2 changes do not affect host build.

- [ ] **Step 3: Confirm the PRX target fails for the right reason**

Run: `OO_PS4_TOOLCHAIN=$OO_PS4_TOOLCHAIN make prx`
Expected: missing `src/main.c`, `src/log_ps4.c`, `src/mqtt/mqtt_socket_ps4.c`, etc. — exactly what the rest of Phase 2 will create.

- [ ] **Step 4: Commit**

```bash
git add Makefile
git commit -m "build: add PS4 PRX target using OpenOrbis toolchain"
```

---

### Task 15: PS4 logger backend

**Goal:** A PS4-specific logger that writes to klog so `nc <ps4-ip> 9998` shows it.

**Files:**
- Create: `src/log_ps4.c`

- [ ] **Step 1: Write the PS4 backend**

`src/log_ps4.c`:

```c
#include "log.h"

#include <stdarg.h>
#include <stdio.h>

/* GoldHEN exports klog-style printing via sceKernelDebugOutText(). */
extern int sceKernelDebugOutText(int channel, const char *str);

static log_level_t g_min_level = LOG_LEVEL_INFO;

void log_init(log_level_t min_level) {
    g_min_level = min_level;
}

void log_write(log_level_t level, const char *fmt, ...) {
    if (level > g_min_level) return;

    static const char *labels[] = {"ERR", "WARN", "INFO", "DEBUG"};
    char line[512];

    int prefix = snprintf(line, sizeof(line), "[ps4-mqtt][%s] ", labels[level]);
    if (prefix < 0 || prefix >= (int)sizeof(line)) return;

    va_list args;
    va_start(args, fmt);
    int n = vsnprintf(line + prefix, sizeof(line) - prefix - 1, fmt, args);
    va_end(args);
    if (n < 0) return;

    size_t total = (size_t)prefix + (size_t)n;
    if (total >= sizeof(line) - 1) total = sizeof(line) - 2;
    line[total]     = '\n';
    line[total + 1] = '\0';

    sceKernelDebugOutText(0, line);
}
```

- [ ] **Step 2: Confirm host build still passes (nothing to add here)**

Run: `make test`
Expected: pass. Host build ignores `log_ps4.c`.

- [ ] **Step 3: Commit**

```bash
git add src/log_ps4.c
git commit -m "feat(log): PS4 backend using sceKernelDebugOutText"
```

---

### Task 16: PS4 socket backend (libNet)

**Goal:** Implement `mqtt_socket.h` against `libSceNet`, with the same semantics as the host backend.

**Files:**
- Create: `src/mqtt/mqtt_socket_ps4.c`

Reference: GoldHEN samples include `Sample_NetSocket` showing the pattern for `sceNetSocket`/`sceNetConnect`/`sceNetSend`/`sceNetRecv`/`sceNetSocketClose`. Read it before implementing.

- [ ] **Step 1: Write the PS4 backend**

`src/mqtt/mqtt_socket_ps4.c`:

```c
#include "mqtt_socket.h"

#include <string.h>

/* libSceNet declarations (subset). */
typedef int SceNetId;

#define SCE_NET_AF_INET     2
#define SCE_NET_SOCK_STREAM 1
#define SCE_NET_IPPROTO_TCP 6
#define SCE_NET_SOL_SOCKET  0xFFFF
#define SCE_NET_SO_RCVTIMEO 0x1006

typedef struct SceNetInAddr { unsigned int s_addr; } SceNetInAddr;

typedef struct SceNetSockaddrIn {
    unsigned char  sin_len;
    unsigned char  sin_family;
    unsigned short sin_port;
    SceNetInAddr   sin_addr;
    unsigned short sin_vport;
    char           sin_zero[6];
} SceNetSockaddrIn;

extern SceNetId sceNetSocket(const char *name, int family, int type, int protocol);
extern int      sceNetConnect(SceNetId s, const SceNetSockaddrIn *addr, int addrlen);
extern int      sceNetSend(SceNetId s, const void *buf, int len, int flags);
extern int      sceNetRecv(SceNetId s, void *buf, int len, int flags);
extern int      sceNetSocketClose(SceNetId s);
extern int      sceNetSetsockopt(SceNetId s, int level, int name,
                                 const void *val, int vallen);
extern int      sceNetInetPton(int af, const char *src, void *dst);
extern unsigned short sceNetHtons(unsigned short host);

int mqtt_socket_connect(const char *host, int port) {
    SceNetId s = sceNetSocket("ps4mqtt", SCE_NET_AF_INET,
                              SCE_NET_SOCK_STREAM, SCE_NET_IPPROTO_TCP);
    if (s < 0) return -1;

    SceNetSockaddrIn addr = {0};
    addr.sin_len    = sizeof(addr);
    addr.sin_family = SCE_NET_AF_INET;
    addr.sin_port   = sceNetHtons((unsigned short)port);
    if (sceNetInetPton(SCE_NET_AF_INET, host, &addr.sin_addr) <= 0) {
        sceNetSocketClose(s);
        return -1;
    }

    if (sceNetConnect(s, &addr, sizeof(addr)) < 0) {
        sceNetSocketClose(s);
        return -1;
    }
    return s;
}

int mqtt_socket_send(int fd, const void *buf, size_t len) {
    const char *p = buf;
    size_t sent = 0;
    while (sent < len) {
        int n = sceNetSend(fd, p + sent, (int)(len - sent), 0);
        if (n <= 0) return -1;
        sent += (size_t)n;
    }
    return (int)sent;
}

int mqtt_socket_recv(int fd, void *buf, size_t len, int timeout_ms) {
    if (timeout_ms >= 0) {
        struct { unsigned int sec; unsigned int usec; } tv;
        tv.sec  = (unsigned)(timeout_ms / 1000);
        tv.usec = (unsigned)((timeout_ms % 1000) * 1000);
        sceNetSetsockopt(fd, SCE_NET_SOL_SOCKET, SCE_NET_SO_RCVTIMEO,
                         &tv, sizeof(tv));
    }
    int n = sceNetRecv(fd, buf, (int)len, 0);
    if (n < 0) return -1;
    return n; /* 0 indicates timeout (matches host semantics) */
}

void mqtt_socket_close(int fd) {
    if (fd >= 0) sceNetSocketClose(fd);
}
```

- [ ] **Step 2: Verify host build is untouched**

Run: `make test`
Expected: pass.

- [ ] **Step 3: Commit**

```bash
git add src/mqtt/mqtt_socket_ps4.c
git commit -m "feat(mqtt): PS4 socket backend using libSceNet"
```

---

### Task 17: PS4 system collector

**Goal:** Real `collect_system` using `sysctl` and `clock_gettime`.

**Files:**
- Create: `src/collectors/system_ps4.c`

- [ ] **Step 1: Write the PS4 implementation**

`src/collectors/system_ps4.c`:

```c
#include "collectors.h"

#include <stdio.h>
#include <string.h>
#include <sys/sysctl.h>
#include <sys/time.h>
#include <time.h>

int collect_system(system_data_t *out) {
    if (!out) return -1;
    memset(out, 0, sizeof(*out));

    /* Uptime via CLOCK_UPTIME. */
    struct timespec ts;
    if (clock_gettime(CLOCK_UPTIME, &ts) == 0) {
        out->uptime_sec = (int64_t)ts.tv_sec;
    }

    /* Total memory: sysctl hw.physmem (bytes). */
    int mib[2] = {6 /*CTL_HW*/, 5 /*HW_PHYSMEM*/};
    unsigned long physmem = 0;
    size_t len = sizeof(physmem);
    if (sysctl(mib, 2, &physmem, &len, NULL, 0) == 0) {
        out->mem_total_mb = (uint64_t)(physmem >> 20);
    }

    /* Used memory: vm.stats.vm.v_active_count * page_size.
     * We approximate "used" as active+wire pages. */
    int v_active = 0, v_wire = 0, page_size = 4096;
    size_t sz;
    sz = sizeof(v_active);
    sysctlbyname("vm.stats.vm.v_active_count", &v_active, &sz, NULL, 0);
    sz = sizeof(v_wire);
    sysctlbyname("vm.stats.vm.v_wire_count",   &v_wire,   &sz, NULL, 0);
    sz = sizeof(page_size);
    sysctlbyname("hw.pagesize", &page_size, &sz, NULL, 0);
    out->mem_used_mb =
        (uint64_t)((unsigned long long)(v_active + v_wire) * page_size >> 20);

    /* Firmware: kern.osrelease (best-effort string). */
    sz = sizeof(out->firmware);
    if (sysctlbyname("kern.osrelease", out->firmware, &sz, NULL, 0) != 0) {
        strncpy(out->firmware, "unknown", sizeof(out->firmware) - 1);
    }
    return 0;
}
```

- [ ] **Step 2: Confirm host build still works (this file is PS4-only)**

Run: `make test`
Expected: pass. Host stub is what gets linked for tests.

- [ ] **Step 3: Commit**

```bash
git add src/collectors/system_ps4.c
git commit -m "feat(collectors): PS4 system collector via sysctl + CLOCK_UPTIME"
```

---

### Task 18: PS4 thermal collector

**Goal:** Read CPU/SoC temperature and fan RPM from the ICC sensors.

GoldHEN exposes ICC temperature via the syscall `sceKernelGetCpuTemperature` (returns °C) and `sceKernelGetSocSensorTemperature`. Fan RPM is at `machdep.fan_speed`. If a syscall is not available, log warning and leave the field at 0.

**Files:**
- Create: `src/collectors/thermal_ps4.c`

- [ ] **Step 1: Write the PS4 implementation**

`src/collectors/thermal_ps4.c`:

```c
#include "collectors.h"
#include "../log.h"

#include <string.h>
#include <sys/sysctl.h>

extern int sceKernelGetCpuTemperature(int *out_celsius);
extern int sceKernelGetSocSensorTemperature(int sensor_id, int *out_celsius);

int collect_thermal(thermal_data_t *out) {
    if (!out) return -1;
    memset(out, 0, sizeof(*out));

    int cpu_c = 0;
    if (sceKernelGetCpuTemperature(&cpu_c) == 0) {
        out->cpu_temp_c = (double)cpu_c;
    } else {
        LOG_WARN("thermal: sceKernelGetCpuTemperature failed");
    }

    int soc_c = 0;
    if (sceKernelGetSocSensorTemperature(0, &soc_c) == 0) {
        out->soc_temp_c = (double)soc_c;
    } else {
        LOG_WARN("thermal: sceKernelGetSocSensorTemperature failed");
    }

    int fan_rpm = 0;
    size_t sz = sizeof(fan_rpm);
    if (sysctlbyname("machdep.fan_speed", &fan_rpm, &sz, NULL, 0) == 0) {
        out->fan_rpm = fan_rpm;
    }
    return 0;
}
```

- [ ] **Step 2: Verify host build untouched**

Run: `make test`
Expected: pass.

- [ ] **Step 3: Commit**

```bash
git add src/collectors/thermal_ps4.c
git commit -m "feat(collectors): PS4 thermal via ICC + machdep.fan_speed"
```

---

### Task 19: PS4 network collector

**Goal:** Read IP, SSID, RSSI via `libSceNetCtl`.

**Files:**
- Create: `src/collectors/network_ps4.c`

- [ ] **Step 1: Write the PS4 implementation**

`src/collectors/network_ps4.c`:

```c
#include "collectors.h"
#include "../log.h"

#include <string.h>

#define SCE_NET_CTL_INFO_IP_ADDRESS  14
#define SCE_NET_CTL_INFO_SSID         8
#define SCE_NET_CTL_INFO_RSSI_DBM    11

typedef union {
    char ip_address[16];
    char ssid[33];
    int  rssi_dbm;
    char raw[64];
} SceNetCtlInfo;

extern int sceNetCtlInit(void);
extern int sceNetCtlGetInfo(int code, SceNetCtlInfo *info);
extern void sceNetCtlTerm(void);

int collect_network(network_data_t *out) {
    if (!out) return -1;
    memset(out, 0, sizeof(*out));

    if (sceNetCtlInit() != 0) {
        LOG_WARN("network: sceNetCtlInit failed");
        return -1;
    }

    SceNetCtlInfo info;

    if (sceNetCtlGetInfo(SCE_NET_CTL_INFO_IP_ADDRESS, &info) == 0) {
        strncpy(out->ip, info.ip_address, sizeof(out->ip) - 1);
    }
    if (sceNetCtlGetInfo(SCE_NET_CTL_INFO_SSID, &info) == 0) {
        strncpy(out->ssid, info.ssid, sizeof(out->ssid) - 1);
    }
    if (sceNetCtlGetInfo(SCE_NET_CTL_INFO_RSSI_DBM, &info) == 0) {
        out->rssi_dbm = info.rssi_dbm;
    }

    sceNetCtlTerm();
    return 0;
}
```

- [ ] **Step 2: Verify host build untouched**

Run: `make test`
Expected: pass.

- [ ] **Step 3: Commit**

```bash
git add src/collectors/network_ps4.c
git commit -m "feat(collectors): PS4 network via libSceNetCtl"
```

---

### Task 20: PS4 storage collector

**Goal:** Read disk usage of `/user/data` (where games install) using `statfs`.

**Files:**
- Create: `src/collectors/storage_ps4.c`

- [ ] **Step 1: Write the PS4 implementation**

`src/collectors/storage_ps4.c`:

```c
#include "collectors.h"
#include "../log.h"

#include <string.h>
#include <sys/mount.h>

int collect_storage(storage_data_t *out) {
    if (!out) return -1;
    memset(out, 0, sizeof(*out));

    struct statfs sfs;
    if (statfs("/user", &sfs) != 0) {
        LOG_WARN("storage: statfs(/user) failed");
        return -1;
    }
    uint64_t total_bytes = (uint64_t)sfs.f_blocks * (uint64_t)sfs.f_bsize;
    uint64_t free_bytes  = (uint64_t)sfs.f_bavail * (uint64_t)sfs.f_bsize;
    uint64_t used_bytes  = total_bytes - free_bytes;
    out->total_gb = total_bytes >> 30;
    out->used_gb  = used_bytes  >> 30;
    return 0;
}
```

- [ ] **Step 2: Verify host build untouched**

Run: `make test`
Expected: pass.

- [ ] **Step 3: Commit**

```bash
git add src/collectors/storage_ps4.c
git commit -m "feat(collectors): PS4 storage via statfs(/user)"
```

---

### Task 21: PS4 app collector

**Goal:** Detect the foreground game using GoldHEN's app helpers. If no game is running (Shell/menu), report `in_game = 0`.

GoldHEN exposes `sceLncUtilGetAppId(pid)`-style APIs. The simplest approach is `sceSystemServiceGetAppIdOfBigApp()` which returns the title id of the foreground "big" app, plus `sceAppInstUtilAppGetTitle()` for the human title.

**Files:**
- Create: `src/collectors/app_ps4.c`

- [ ] **Step 1: Write the PS4 implementation**

`src/collectors/app_ps4.c`:

```c
#include "collectors.h"
#include "../log.h"

#include <string.h>

extern int sceSystemServiceGetAppIdOfBigApp(void);
/* GoldHEN helper that resolves a PID/AppId into a title id buffer. */
extern int sceLncUtilGetAppTitleId(int app_id, char *out, int out_len);
/* App database: human-readable title for a title id. */
extern int sceAppInstUtilAppGetTitle(const char *title_id,
                                     char *out, int out_len);

int collect_app(app_data_t *out) {
    if (!out) return -1;
    memset(out, 0, sizeof(*out));

    int app_id = sceSystemServiceGetAppIdOfBigApp();
    if (app_id <= 0) {
        out->in_game = 0;
        return 0;
    }

    char title_id[16] = {0};
    if (sceLncUtilGetAppTitleId(app_id, title_id, sizeof(title_id)) != 0) {
        out->in_game = 0;
        return 0;
    }

    char title[64] = {0};
    if (sceAppInstUtilAppGetTitle(title_id, title, sizeof(title)) != 0) {
        strncpy(title, title_id, sizeof(title) - 1);
    }

    out->in_game = 1;
    strncpy(out->title,    title,    sizeof(out->title)    - 1);
    strncpy(out->title_id, title_id, sizeof(out->title_id) - 1);
    return 0;
}
```

If during smoke testing the symbols above don't resolve, the engineer should consult `goldhen.h` from the GoldHEN Plugin SDK and substitute the actual exports (e.g. `sceLncUtilGetAppId(getpid())` followed by the SDK's title-id helper).

- [ ] **Step 2: Verify host build untouched**

Run: `make test`
Expected: pass.

- [ ] **Step 3: Commit**

```bash
git add src/collectors/app_ps4.c
git commit -m "feat(collectors): PS4 app collector via SystemService + Lnc"
```

---

### Task 22: Plugin entry point and background thread

**Goal:** GoldHEN expects exported `attr_module_hidden int module_start(...)` and `module_stop(...)`. We start a pthread on load that loops `publisher_run_once` every `poll_interval_sec`, and tear it down on unload.

**Files:**
- Create: `src/main.c`

- [ ] **Step 1: Write the entry point**

`src/main.c`:

```c
#include "config.h"
#include "log.h"
#include "publisher.h"
#include "ha/ha_discovery.h"
#include "mqtt/mqtt_client.h"

#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#define CONFIG_PATH "/data/GoldHEN/plugins/ps4-mqtt/config.json"

static pthread_t      g_thread;
static volatile int   g_stop;
static mqtt_client_t *g_client;
static config_t       g_cfg;
static char           g_slug[CFG_DEVNAME_MAX];

static int mqtt_publish_adapter(void *ctx, const char *topic,
                                const char *payload, int retain) {
    return mqtt_client_publish((mqtt_client_t *)ctx, topic, payload, retain);
}

static void publish_discovery(mqtt_client_t *c, const char *slug,
                              const char *device_name, const char *fw) {
    ha_device_t dev = { .slug = slug, .name = device_name, .fw = fw };
    char topic[160];
    char payload[768];

    struct sensor_def {
        const char *key;
        const char *friendly;
        const char *subtopic;
        const char *unit;
        const char *device_class;
        const char *state_class;
    };
    static const struct sensor_def sensors[] = {
        {"cpu_temp",      "CPU Temperature",      "cpu/temp",        "°C", "temperature", "measurement"},
        {"soc_temp",      "SoC Temperature",      "soc/temp",        "°C", "temperature", "measurement"},
        {"fan_rpm",       "Fan Speed",            "fan/rpm",         "rpm",     "",            "measurement"},
        {"memory_used",   "Memory Used",          "memory/used_mb",  "MB",      "data_size",   "measurement"},
        {"memory_total",  "Memory Total",         "memory/total_mb", "MB",      "data_size",   "measurement"},
        {"network_rssi",  "WiFi Signal",          "network/rssi",    "dBm",     "signal_strength", "measurement"},
        {"storage_used",  "Storage Used",         "storage/used_gb", "GB",      "data_size",   "measurement"},
        {"storage_total", "Storage Total",        "storage/total_gb","GB",      "data_size",   "measurement"},
        {"uptime_sec",    "Uptime",               "uptime_sec",      "s",       "duration",    "total_increasing"},
    };

    for (size_t i = 0; i < sizeof(sensors)/sizeof(sensors[0]); ++i) {
        const struct sensor_def *s = &sensors[i];
        snprintf(topic, sizeof(topic),
                 "homeassistant/sensor/ps4_%s_%s/config", slug, s->key);
        if (ha_build_sensor_config(payload, sizeof(payload), &dev,
                                   s->key, s->friendly, s->subtopic,
                                   s->unit, s->device_class, s->state_class) == 0) {
            mqtt_client_publish(c, topic, payload, /*retain*/ 1);
        }
    }

    /* state as a binary_sensor */
    snprintf(topic, sizeof(topic),
             "homeassistant/binary_sensor/ps4_%s_state/config", slug);
    if (ha_build_binary_sensor_config(payload, sizeof(payload), &dev,
                                      "state", "PS4 State", "state",
                                      "on", "standby") == 0) {
        mqtt_client_publish(c, topic, payload, 1);
    }
}

static void *worker_main(void *arg) {
    (void)arg;
    while (!g_stop) {
        if (!mqtt_client_is_connected(g_client)) {
            /* Reconnect with capped exponential backoff. */
            static const int delays[] = {5, 10, 30, 60};
            for (size_t i = 0; !g_stop; ++i) {
                if (mqtt_client_connect(g_client) == 0) {
                    char avail_topic[128];
                    snprintf(avail_topic, sizeof(avail_topic),
                             "ps4/%s/availability", g_slug);
                    mqtt_client_publish(g_client, avail_topic, "online", 1);
                    publish_discovery(g_client, g_slug,
                                      g_cfg.device_name, "11.00");
                    break;
                }
                int d = delays[i < 4 ? i : 3];
                for (int s = 0; s < d && !g_stop; ++s) sleep(1);
            }
            continue;
        }

        publisher_t pub = {
            .slug    = g_slug,
            .publish = mqtt_publish_adapter,
            .ctx     = g_client,
        };
        publisher_run_once(&pub);

        /* Sleep poll_interval_sec, with periodic ping (every 60 s). */
        for (int s = 0; s < g_cfg.poll_interval_sec && !g_stop; ++s) {
            sleep(1);
        }
        mqtt_client_ping(g_client); /* keepalive */
    }
    return NULL;
}

/* GoldHEN plugin entry points. */
__attribute__((visibility("default")))
int module_start(size_t argc, const void *argv) {
    (void)argc; (void)argv;
    log_init(LOG_LEVEL_INFO);

    if (config_load(CONFIG_PATH, &g_cfg) != 0) {
        LOG_ERR("plugin: config load failed; not starting worker");
        return 0; /* never block GoldHEN load */
    }
    if (ha_make_slug(g_cfg.device_name, g_slug, sizeof(g_slug)) != 0) {
        LOG_ERR("plugin: invalid device_name");
        return 0;
    }

    char client_id[64];
    snprintf(client_id, sizeof(client_id), "ps4-mqtt-%s", g_slug);
    char will_topic[128];
    snprintf(will_topic, sizeof(will_topic), "ps4/%s/availability", g_slug);

    g_client = mqtt_client_new(g_cfg.broker_host, g_cfg.broker_port,
                               g_cfg.username, g_cfg.password,
                               client_id, /*keepalive*/ 60,
                               will_topic, "offline");
    if (!g_client) {
        LOG_ERR("plugin: mqtt_client_new failed");
        return 0;
    }

    g_stop = 0;
    if (pthread_create(&g_thread, NULL, worker_main, NULL) != 0) {
        LOG_ERR("plugin: pthread_create failed");
        mqtt_client_free(g_client);
        g_client = NULL;
        return 0;
    }
    LOG_INFO("plugin: started; broker=%s:%d device=%s",
             g_cfg.broker_host, g_cfg.broker_port, g_cfg.device_name);
    return 0;
}

__attribute__((visibility("default")))
int module_stop(size_t argc, const void *argv) {
    (void)argc; (void)argv;
    g_stop = 1;
    if (g_thread) {
        pthread_join(g_thread, NULL);
        g_thread = 0;
    }
    if (g_client) {
        char avail_topic[128];
        snprintf(avail_topic, sizeof(avail_topic),
                 "ps4/%s/availability", g_slug);
        mqtt_client_publish(g_client, avail_topic, "offline", 1);
        mqtt_client_free(g_client);
        g_client = NULL;
    }
    LOG_INFO("plugin: stopped");
    return 0;
}
```

- [ ] **Step 2: Build the PRX**

Run: `OO_PS4_TOOLCHAIN=$OO_PS4_TOOLCHAIN make prx`
Expected: produces `build/ps4-mqtt.prx` with no errors. If any external symbol is unresolved, fix the `extern` prototypes in the offending file using the GoldHEN Plugin SDK headers and re-run.

- [ ] **Step 3: Confirm host tests still pass**

Run: `make test`
Expected: pass.

- [ ] **Step 4: Commit**

```bash
git add src/main.c
git commit -m "feat(plugin): module_start/stop, worker thread, HA discovery on connect"
```

---

### Task 23: README and config example updates

**Goal:** Document install, config, and smoke-test steps so anyone (or a future you) can deploy.

**Files:**
- Modify: `README.md`

- [ ] **Step 1: Replace `README.md`**

Replace the existing `README.md` with:

````markdown
# ps4-mqtt-plugin

GoldHEN plugin for jailbroken PlayStation 4 (FW 11.00 target) that
publishes console telemetry to an MQTT broker for Home Assistant.

Sensors auto-create via Home Assistant MQTT Discovery: console state,
current game, CPU/SoC temperature, fan speed, memory usage, network
(IP/SSID/RSSI), storage, uptime, firmware.

## Requirements

- PlayStation 4, jailbroken, GoldHEN 2.4b18.3 or later
- Mosquitto (or compatible) MQTT broker reachable from the PS4
- Home Assistant with the MQTT integration enabled

## Build

### Host tests (no PS4 needed)

```bash
make test
```

### Integration tests (needs Mosquitto installed locally)

```bash
make integration
```

### PS4 PRX

```bash
export OO_PS4_TOOLCHAIN=/path/to/OpenOrbis-PS4-Toolchain
make prx
# produces build/ps4-mqtt.prx
```

## Install on PS4

1. Copy `build/ps4-mqtt.prx` to `/data/GoldHEN/plugins/ps4-mqtt/`
2. Copy `config.json` to `/data/GoldHEN/plugins/ps4-mqtt/`
3. Add the plugin path to `/data/GoldHEN/plugins.ini` so GoldHEN auto-loads it
4. Reboot the PS4

## Configure

`config.json` fields (see `config.example.json`):

| Field               | Required | Default | Notes |
|---------------------|----------|---------|-------|
| `broker_host`       | yes      | —       | IP or hostname of MQTT broker |
| `broker_port`       | no       | 1883    | TCP port |
| `username`          | yes      | —       | Broker auth |
| `password`          | yes      | —       | Broker auth |
| `device_name`       | no       | `PS4`   | Shown in Home Assistant |
| `poll_interval_sec` | no       | 10      | How often to publish |

## Verify

After reboot, in Home Assistant: `Settings → Devices & services → MQTT`
should list a device named `PS4 <device_name>` with all sensors. Logs
appear via `nc <ps4-ip> 9998` (GoldHEN klog) prefixed `[ps4-mqtt]`.

## Limitations (MVP)

- No TLS/MQTTS — broker should be on the local trusted network
- CPU% and FPS are not collected (out of scope)
- Wall power consumption cannot be measured (no API)
- Plugin only publishes; SUBSCRIBE is not implemented

## License

TBD
````

- [ ] **Step 2: Commit**

```bash
git add README.md
git commit -m "docs: README install/build/configure instructions"
```

---

### Task 24: Manual smoke test on PS4

**Goal:** End-to-end verification on real hardware. Not a code task — a runbook.

**Files:** none.

- [ ] **Step 1: Stage files on PS4**

Use FTP (GoldHEN's FTP server is on port 2121):

```bash
PS4_IP=192.168.1.50  # adjust
curl -T build/ps4-mqtt.prx ftp://$PS4_IP:2121/data/GoldHEN/plugins/ps4-mqtt/ps4-mqtt.prx
curl -T config.json        ftp://$PS4_IP:2121/data/GoldHEN/plugins/ps4-mqtt/config.json
```

If `/data/GoldHEN/plugins/ps4-mqtt/` does not exist, create it via FTP first.

- [ ] **Step 2: Add plugin to plugins.ini**

Edit `/data/GoldHEN/plugins.ini` on the PS4 (FTP/SSH) and add:

```ini
ps4-mqtt=/data/GoldHEN/plugins/ps4-mqtt/ps4-mqtt.prx
```

- [ ] **Step 3: Reboot PS4 and watch klog**

```bash
nc $PS4_IP 9998
```

Expected lines within 10 seconds of boot:

```
[ps4-mqtt][INFO] plugin: started; broker=...
[ps4-mqtt][INFO] mqtt: connected to ...
```

- [ ] **Step 4: Verify Home Assistant device**

In HA → Settings → Devices & services → MQTT — a device named
`PS4 <device_name>` should appear with all sensors populated.

- [ ] **Step 5: Functional checks**

Each takes <60 s:

| Check | Expected outcome |
|-------|------------------|
| Wait 10 s | All sensors update at least once |
| Open a game | `game/title` and `game/title_id` change |
| Return to home menu | `game/title` becomes empty |
| Stop the broker (`systemctl stop mosquitto`) | HA marks device unavailable within ~30 s (LWT) |
| Start the broker | HA marks online again within `poll_interval_sec` + reconnect backoff |
| Power off PS4 | HA marks unavailable (LWT fires when broker drops the dead client) |

- [ ] **Step 6: Tag the release**

```bash
git tag -a v0.1.0 -m "MVP: PS4 telemetry to Home Assistant via MQTT Discovery"
git push origin v0.1.0
```

---

## Self-Review Notes

- **Spec coverage:** every metric in the spec's MVP table maps to a topic in Task 13 and a discovery payload in Task 22. State, availability/LWT, and reconnect backoff appear in Tasks 22 and 10. Config parsing matches spec defaults exactly. HA Discovery format follows the example in the spec.
- **Type consistency:** `mqtt_publish_fn` signature is fixed at Task 13 and reused unchanged in Task 22. `config_t` field names match between header (Task 4) and consumers (Task 22). `ha_device_t` shape matches spec snippet.
- **Out-of-scope items** (CPU%, FPS, wall power, TLS) are explicitly listed in spec Non-Goals; this plan does not contain tasks for them.
