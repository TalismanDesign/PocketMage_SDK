---
title: "Building an app"
description: "The toolchain, the project layout, and the make targets that produce a shippable ELF and tar."
---

# Building an app

The whole build system is one `Makefile` that includes the SDK recipe in
`tools/app.mk`.

## Project layout

```text
myapp/
  Makefile       # include of tools/app.mk
  main.cpp
  myapp_ICON.bin # optional 40x40 icon, see Icon below
  src/i18n/      # optional app translation catalog
```

```make
SDK_ROOT ?= $(abspath ../..)      # path to this repo
include $(SDK_ROOT)/tools/app.mk
```

Everything is overridable on the command line or in the `Makefile`:
`APP_SRCS` (default `main.cpp`), `APP_NAME` (default the directory basename),
`APP_BUILD_DIR` (default `build`), `XTENSA_CXX` (default: probed toolchain).

## Toolchain

| Requirement | Value                                      |
| ----------- | ------------------------------------------ |
| Compiler    | espressif `xtensa-esp32s3-elf-g++`         |
| Output      | little-endian (required by the loader)     |

The PlatformIO `toolchain-xtensa-esp32s3` package emits big-endian ELFs and
is unusable for apps; if it is the only compiler found, `app.mk` fails the
build with a clear error. Install the espressif toolchain so it lands at one
of the probed paths (newest first):

```sh
~/.espressif/tools/xtensa-esp-elf/esp-15.2.0_20251204/xtensa-esp-elf/bin
~/.espressif/tools/xtensa-esp-elf/esp-14.2.0_20251107/xtensa-esp-elf/bin
~/.espressif/tools/xtensa-esp-elf/esp-14.2.0_20241119/xtensa-esp-elf/bin
```

`esp-15.2.0_20251204` is the one SDK CI pins (URL and SHA256). It is the
easiest known-good install:

```sh
VER=esp-15.2.0_20251204
SHA=3d50f5cd5f173acfd524e07c1cd69bc99585731a415ca2e5bce879997fe602b8
URL="https://github.com/espressif/crosstool-NG/releases/download/$VER/xtensa-esp-elf-15.2.0_20251204-x86_64-linux-gnu.tar.xz"
mkdir -p "$HOME/.espressif/tools/xtensa-esp-elf/$VER"
curl -fsSL -o /tmp/xtensa-esp-elf.tar.xz "$URL"
echo "$SHA  /tmp/xtensa-esp-elf.tar.xz" | sha256sum -c -
tar -xJf /tmp/xtensa-esp-elf.tar.xz -C "$HOME/.espressif/tools/xtensa-esp-elf/$VER"
```

The example builds with any probed version. The host OS pins its own
toolchain independently of the SDK.

## Targets

| Target        | Result                                                        |
| ------------- | ------------------------------------------------------------- |
| `make`        | Builds `build/<name>.app.elf`, stripped, endianness-asserted  |
| `make check`  | Prints the entry point and every undefined symbol (the app's host dependency list) |
| `make pack`   | Builds `build/<name>.tar` with `<name>.app.elf` and `<name>_ICON.bin` (see [publish.md](publish.md)) |
| `make clean`  | Removes `build/`                                              |

The endianness assertion runs after every link. A big-endian artifact gets
deleted and the build fails.

## Entry point

`extern "C" int main(int argc, char **argv)`, renamed to `app_main` via
`-Dmain=app_main -e app_main`. See [app-abi.md](app-abi.md) for the full
contract. Everything the app calls resolves from the host at load time, so
keep the undefined-symbol list (from `make check`) small and intentional; see
[symbols.md](symbols.md).

## Compiler flags

The SDK builds with `-Wall -Wextra`, exceptions and RTTI disabled, and hidden
visibility. Keep the code warning-free.

## Icon

`<name>_ICON.bin` is an optional 200-byte file: a 40x40, 1-bit-per-pixel
bitmap (5 bytes per row, 8 pixels per byte), drawn by the OS as the app icon.
Missing icons fall back to the OS default. `make pack` requires the file next
to the `Makefile`; without one the pack step errors.

## Translations

An app that shows strings uses its own catalog; see [i18n.md](i18n.md). The
short version: put a `src/i18n/` tree in your app and merge it via the
generator the way `platformio.ini` shows in that doc.

## Before you ship

Run `make clean all check` before every commit. `make pack` is the
deliverable; verify the tar's members with `tar -tf build/<name>.tar`.