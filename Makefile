# Host build (tests). PS4 build is added in Phase 2.
CC       ?= cc
CFLAGS    = -std=c99 -Wall -Wextra -Wpedantic -O0 -g \
            -Isrc -Isrc/mqtt -Ithird_party/minunit -Ithird_party/cJSON
LDFLAGS   =

BUILD_DIR = build
TEST_BIN  = $(BUILD_DIR)/tests

# Test sources are added as more tasks land.
LIB_HOST_SOURCES = \
    src/log_host.c \
    src/config.c \
    src/mqtt/mqtt_packet.c \
    src/mqtt/mqtt_socket_host.c \
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
