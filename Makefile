# Host build (tests). PS4 build is added in Phase 2.
CC       ?= cc
CFLAGS    = -std=c99 -Wall -Wextra -Wpedantic -O0 -g \
            -Isrc -Isrc/mqtt -Isrc/ha -Isrc/collectors \
            -Ithird_party/minunit -Ithird_party/cJSON
LDFLAGS   =

BUILD_DIR = build
TEST_BIN  = $(BUILD_DIR)/tests

# Test sources are added as more tasks land.
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

TEST_SOURCES = \
    tests/test_smoke.c \
    $(LIB_HOST_SOURCES)

.PHONY: all test clean

all: test

$(BUILD_DIR):
	mkdir -p $(BUILD_DIR)

test: $(BUILD_DIR)
	$(CC) $(CFLAGS) $(TEST_SOURCES) -o $(TEST_BIN) $(LDFLAGS)
	$(TEST_BIN)

clean:
	rm -rf $(BUILD_DIR)

INTEGRATION_BIN     = $(BUILD_DIR)/integration
INTEGRATION_SOURCES = tests/integration/test_mqtt_integration.c \
                      $(LIB_HOST_SOURCES) \
                      src/mqtt/mqtt_client.c

.PHONY: integration

integration: $(BUILD_DIR)
	$(CC) $(CFLAGS) $(INTEGRATION_SOURCES) -o $(INTEGRATION_BIN) $(LDFLAGS)
	./tests/integration/run_integration.sh
