# PocketMage SDK external-app build recipe.
#
# Nothing is linked against the app: newlib, libstdc++ and the SDK are all left undefined 
# so the host resolves them at load time via its libc / IDF / customer export tables.
#
# Include from a per-app Makefile via:
#   SDK_ROOT ?= <path to the PocketMage SDK repo>
#   include $(SDK_ROOT)/tools/app.mk
#
# Overridable variables:
#   SDK_ROOT        (required) SDK checkout root
#   APP_SRCS        source files, default main.cpp
#   APP_NAME        artifact name, default current directory basename
#   APP_BUILD_DIR   output dir, default build
#   XTENSA_CXX      xtensa g++, default probes the espressif toolchains
#   XTENSA_STRIP    derived from XTENSA_CXX unless set
#   XTENSA_READELF  derived from XTENSA_CXX unless set
#
# The app entry point is app_main. Write main() with C linkage so the mangled
# name matches the linker -e entry:
#   extern "C" int main(int argc, char **argv);
# The Arduino-style API (setup/loop) is layered on top by the app-runtime shim.

APP_SRCS    ?= main.cpp
APP_NAME    ?= $(notdir $(CURDIR))
APP_BUILD_DIR ?= build
APP_OUT     := $(APP_BUILD_DIR)/$(APP_NAME).app.elf
APP_ICON    ?= $(CURDIR)/$(APP_NAME)_ICON.bin
APP_TAR     := $(APP_BUILD_DIR)/$(APP_NAME).tar

SDK_ROOT    ?= $(error SDK_ROOT must be set)

# toolchain

# Probe newest to oldest: espressif toolchain dirs, then the PlatformIO
# package, then whatever is on PATH.
ESP_TOOLCHAIN_BIN := $(firstword $(wildcard \
  $(HOME)/.espressif/tools/xtensa-esp-elf/esp-15.2.0_20251204/xtensa-esp-elf/bin \
  $(HOME)/.espressif/tools/xtensa-esp-elf/esp-14.2.0_20251107/xtensa-esp-elf/bin \
  $(HOME)/.espressif/tools/xtensa-esp-elf/esp-14.2.0_20241119/xtensa-esp-elf/bin \
  $(HOME)/.platformio/packages/toolchain-xtensa-esp32s3/bin))

XTENSA_CXX ?= $(if $(ESP_TOOLCHAIN_BIN),$(ESP_TOOLCHAIN_BIN)/xtensa-esp32s3-elf-g++,$(shell command -v xtensa-esp32s3-elf-g++ 2>/dev/null))
XTENSA_STRIP   ?= $(XTENSA_CXX:-g++=-strip)
XTENSA_READELF ?= $(XTENSA_CXX:-g++=-readelf)

# compile / link

# -Dmain=app_main and -e app_main wire the C entry point to the ELF entry.
APP_CXXFLAGS := -std=gnu++17 -fPIC -Dmain=app_main
APP_CXXFLAGS += -fdata-sections -ffunction-sections -fvisibility=hidden
APP_CXXFLAGS += -fno-exceptions -fno-rtti -fno-threadsafe-statics
APP_CXXFLAGS += -Wall -Wextra
APP_CPPFLAGS += -I$(SDK_ROOT) -DPM_TARGET_APP=1
APP_LDFLAGS  := -nostartfiles -nostdlib -fPIC -shared -e app_main
APP_LDFLAGS  += -fdata-sections -ffunction-sections -Wl,--gc-sections
APP_LDFLAGS  += -fvisibility=hidden
APP_LDFLAGS  += -Wl,--strip-all -Wl,--strip-debug -Wl,--strip-discarded
APP_LDFLAGS  += -Dmain=app_main

# Post-link strip: drop loader-irrelevant and duplicate sections.
APP_STRIP_FLAGS := --strip-unneeded --remove-section=.comment
APP_STRIP_FLAGS += --remove-section=.got.loc --remove-section=.dynamic
APP_STRIP_FLAGS += --remove-section=.xt.lit --remove-section=.xt.prop
APP_STRIP_FLAGS += --remove-section=.xtensa.info

APP_OBJS := $(patsubst %,$(APP_BUILD_DIR)/%.o,$(basename $(APP_SRCS)))

.PHONY: all elf check clean pack
all: $(APP_OUT)

elf: $(APP_OUT)

$(APP_OUT): $(APP_SRCS) | $(APP_BUILD_DIR)
	$(XTENSA_CXX) $(APP_CPPFLAGS) $(APP_CXXFLAGS) $(APP_SRCS) $(APP_LDFLAGS) -o $@.raw
	$(XTENSA_STRIP) $(APP_STRIP_FLAGS) $@.raw -o $@
	rm -f $@.raw
	@if ! $(XTENSA_READELF) -h $@ 2>/dev/null | grep -q 'little endian'; then \
	  echo "error: $(APP_NAME).app.elf is big-endian; the loader requires a little-endian ELF (use the xtensa-esp-elf toolchain)" >&2; \
	  rm -f $@; exit 1; \
	fi
	@echo "built $(APP_NAME).app.elf ($$(wc -c < $@) bytes)"

$(APP_BUILD_DIR):
	mkdir -p $@

# Verify the artifact
check: $(APP_OUT)
	@echo "entry: $$($(XTENSA_READELF) -h $< | awk '/Entry point/ {print $$4}')"
	@echo "undefined symbols (resolved by host at load time):"
	@$(XTENSA_READELF) -s -W $< | awk '$$5 ~ /GLOBAL/ && $$7 == "UND" {print $$8}' | sort -u

pack: $(APP_OUT) $(APP_ICON)
	@tar -cf $(APP_TAR) -C $(APP_BUILD_DIR) $(APP_NAME).app.elf
	@tar -uf $(APP_TAR) -C $(CURDIR) $(APP_NAME)_ICON.bin
	@echo "packed $(APP_TAR) ($$(wc -c < $(APP_TAR)) bytes)"
	@tar -tvf $(APP_TAR)

clean:
	rm -rf $(APP_BUILD_DIR)