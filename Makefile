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

# ----- PS4 PRX build (Phase 2) ---------------------------------------------
# Requires OpenOrbis toolchain. Run via scripts/build-prx.sh which uses
# Docker, or set OO_PS4_TOOLCHAIN to a native install.

OO          ?= $(OO_PS4_TOOLCHAIN)
PS4_CC       = clang
PS4_LD       = ld.lld
PS4_CREATE   = $(OO)/bin/linux/create-fself

PS4_CFLAGS   = --target=x86_64-pc-freebsd12-elf \
               -fPIC -funwind-tables \
               -isysroot $(OO) -isystem $(OO)/include \
               -O2 -std=c99 \
               -D_POSIX_C_SOURCE=200809L \
               -Isrc -Isrc/mqtt -Isrc/ha -Isrc/collectors \
               -Ithird_party/cJSON
PS4_LDFLAGS  = -m elf_x86_64 -pie --script $(OO)/link.x -e _init --eh-frame-hdr \
               --export-dynamic \
               -L$(OO)/lib \
               -lSceLibcInternal -lkernel -lSceSysmodule -lSceNet -lScePosix \
               -lSceSystemService -lSceLncUtil -lScePad -lSceUserService \
               -lSceAppInstUtil

PS4_SOURCES  = \
    src/main.c \
    src/log_ps4.c \
    src/mqtt/mqtt_packet.c \
    src/mqtt/mqtt_socket_ps4.c \
    src/mqtt/mqtt_client.c \
    src/collectors/system_ps4.c \
    src/collectors/app_ps4.c \
    src/collectors/controller_ps4.c

PS4_OBJ_DIR  = $(BUILD_DIR)/ps4
PS4_OBJS     = $(PS4_SOURCES:%.c=$(PS4_OBJ_DIR)/%.o)
PS4_ELF      = $(BUILD_DIR)/ps4-mqtt.elf
PS4_PRX      = $(BUILD_DIR)/ps4-mqtt.prx

.PHONY: prx prx-clean

$(PS4_OBJ_DIR)/%.o: %.c
	@mkdir -p $(dir $@)
	$(PS4_CC) $(PS4_CFLAGS) -c $< -o $@

$(PS4_ELF): $(PS4_OBJS)
	$(PS4_LD) $(PS4_LDFLAGS) -o $@ $^

$(PS4_PRX): $(PS4_ELF)
	$(PS4_CREATE) -in=$(PS4_ELF) -out=$(BUILD_DIR)/ps4-mqtt.oelf \
	    --lib=$(PS4_PRX) --libname=ps4-mqtt \
	    --paid 0x3800000000000011 --sdkver 72319233

prx: $(PS4_PRX)

prx-clean:
	rm -rf $(PS4_OBJ_DIR) $(PS4_ELF) $(PS4_PRX)
