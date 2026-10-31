---
title: "Building an app"
description: "The toolchain, the project layout, and the make targets that produce a shippable ELF and tar."
---

# Building an app

The whole build system is one `Makefile` that includes the SDK recipe in `tools/app.mk`.

## Project layout

```text
myapp/
  Makefile         # include of tools/app.mk
  main.cpp
  myapp_ICON.bin   # 40x40 icon, see Icon below
  app.properties   # optional manifest: name, version, author, scope
  src/i18n/        # optional app translation catalog
```

```make
SDK_ROOT ?= $(abspath ../..)      # path to this repo
include $(SDK_ROOT)/tools/app.mk
```

Everything is overridable on the command line or in the `Makefile`: `APP_SRCS` (default `main.cpp`), `APP_NAME` (default the directory basename), `APP_BUILD_DIR` (default `build`), `XTENSA_CXX` (default: probed toolchain).

## Toolchain

| Requirement | Value                                      |
| ----------- | ------------------------------------------ |
| Compiler    | espressif `xtensa-esp32s3-elf-g++`         |
| Output      | little-endian (required by the loader)     |

The PlatformIO `toolchain-xtensa-esp32s3` package emits big-endian ELFs and is unusable for apps; if it is the only compiler found, `app.mk` fails the build with a clear error. Install the espressif toolchain so it lands at one of the probed paths (newest first):

```sh
~/.espressif/tools/xtensa-esp-elf/esp-15.2.0_20251204/xtensa-esp-elf/bin
~/.espressif/tools/xtensa-esp-elf/esp-14.2.0_20251107/xtensa-esp-elf/bin
~/.espressif/tools/xtensa-esp-elf/esp-14.2.0_20241119/xtensa-esp-elf/bin
```

`esp-15.2.0_20251204` is the one SDK CI pins (URL and SHA256). It is the easiest known-good install:

```sh
VER=esp-15.2.0_20251204
SHA=3d50f5cd5f173acfd524e07c1cd69bc99585731a415ca2e5bce879997fe602b8
URL="https://github.com/espressif/crosstool-NG/releases/download/$VER/xtensa-esp-elf-15.2.0_20251204-x86_64-linux-gnu.tar.xz"
mkdir -p "$HOME/.espressif/tools/xtensa-esp-elf/$VER"
curl -fsSL -o /tmp/xtensa-esp-elf.tar.xz "$URL"
echo "$SHA  /tmp/xtensa-esp-elf.tar.xz" | sha256sum -c -
tar -xJf /tmp/xtensa-esp-elf.tar.xz -C "$HOME/.espressif/tools/xtensa-esp-elf/$VER"
```

The example builds with any probed version. The host OS pins its own toolchain independently of the SDK.

## Targets

| Target        | Result                                                        |
| ------------- | ------------------------------------------------------------- |
| `make`        | Builds `build/<name>.app.elf` (stripped, endianness-asserted) and `build/<name>.elfo` (the overlay sidecar) |
| `make overlay`| Builds only `build/<name>.elfo`                               |
| `make check`  | Prints the entry point and every undefined symbol (the app's host dependency list). Informational; the enforcing gate is `pm check` |
| `make pack`   | Builds `build/<name>.tar` with `<name>.app.elf`, `<name>.elfo`, `<name>_ICON.bin`, and `app.properties` when present (see [publishing](guides/publishing.md)) |
| `make pm-info`| Prints the resolved paths and toolchain the `pm` CLI reads   |
| `make clean`  | Removes `build/`                                              |

The endianness assertion runs after every link. A big-endian artifact gets deleted and the build fails.

## Overlay emission

Alongside the ELF, `make` produces `build/<name>.elfo`, the overlay blob the host pages into executable RAM at load time. It is built from the unstripped link output in two steps:

```text
elf_overlay_relink <name>.app.tool <name>.relk
elf_overlay_emit   <name>.relk     <name>.elfo
```

Both tools must be on `PATH`. They come from an `elf_overlay` checkout, built with CMake (`cmake -S . -B build && cmake --build build --target elf_overlay_relink elf_overlay_emit`), then the build directory added to `PATH`. Override `ELF_OVERLAY_RELINK` and `ELF_OVERLAY_EMIT` to point at specific binaries. The format and the device-side load sequence are in [overlay.md](overlay.md).

## Entry point

`extern "C" int main(int argc, char **argv)`, renamed to `app_main` via `-Dmain=app_main -e app_main`. See [app-abi.md](app-abi.md) for the full contract. Everything the app calls resolves from the host at load time, so keep the undefined-symbol list (from `make check`) small and intentional; see [symbols.md](symbols.md).

## Compiler flags

The SDK builds with `-Wall -Wextra`, exceptions and RTTI disabled, and hidden visibility. Keep the code warning-free.

`app.mk` injects `PM_SDK_VERSION` plus `PM_SDK_VERSION_MAJOR/MINOR/PATCH` from the repo `VERSION` file, so an app always sees `POCKETMAGE_SDK_VERSION_*` in `pocketmage_app_version.h` alongside the host-exported string literal `pocketmage_sdk_version`; see [app-abi.md](app-abi.md).

## Icon

`<name>_ICON.bin` is a 200-byte file: a 40x40, 1-bit-per-pixel bitmap (5 bytes per row, 8 pixels per byte), drawn by the OS as the app icon. `make pack` requires the file next to the `Makefile` and errors without it. The OS falls back to a default icon only for an installed app that has none.

## Translations

An app that shows strings uses its own catalog; see [i18n.md](i18n.md). The short version: put a `src/i18n/` tree in your app and merge it via the generator the way `platformio.ini` shows in that doc.

## Before you ship

Run `make clean all` then `pm check` before every commit; `pm check` is the gate CI runs, and `--host-elf <firmware.elf>` pins the host export surface exactly (see [symbols.md](symbols.md)). `make pack` is the deliverable; verify the tar's members with `tar -tf build/<name>.tar`. It should list `<name>.app.elf`, `<name>.elfo`, `<name>_ICON.bin`, and `app.properties`.

`pm new myapp` scaffolds from the official [PocketMage_App](https://github.com/TalismanDesign/PocketMage_App) template. The SDK's `pm` CLI is the fastest path from scaffold to shippable tar; the full command surface is in [cli.md](cli.md). CI builds that same template against the SDK on every push, and rejects a `broken_app` fixture that references a symbol the host does not export and so must fail `pm check`.