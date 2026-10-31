---
title: "Install the toolchain"
description: "The pinned espressif Xtensa compiler, the elf_overlay host tools, and the pm CLI an app build needs."
---

# Install the toolchain

An app build needs three things on the machine: the espressif Xtensa compiler, the `elf_overlay` host tools, and the `pm` CLI. Only the espressif toolchain produces output the loader can read; the PlatformIO Xtensa package emits the wrong byte order and is unusable for apps.

## Xtensa compiler

The build targets the espressif `xtensa-esp32s3-elf-g++` toolchain. `app.mk` probes for it in the espressif install directory, newest to oldest, then the PlatformIO package, then `PATH`:

```text
~/.espressif/tools/xtensa-esp-elf/esp-15.2.0_20251204/xtensa-esp-elf/bin
~/.espressif/tools/xtensa-esp-elf/esp-14.2.0_20251107/xtensa-esp-elf/bin
~/.espressif/tools/xtensa-esp-elf/esp-14.2.0_20241119/xtensa-esp-elf/bin
~/.platformio/packages/toolchain-xtensa-esp32s3/bin
```

The pinned release is `esp-15.2.0_20251204`. The simplest way to get it is through ESP-IDF's installer, which places it under `~/.espressif`. If you have an existing install elsewhere, point the build at it:

```sh
make XTENSA_CXX=/path/to/xtensa-esp32s3-elf-g++
```

`XTENSA_STRIP` and `XTENSA_READELF` are derived from `XTENSA_CXX` unless set.

Verify the compiler resolves:

```sh
make -C <app> pm-info     # prints the resolved toolchain and paths
```

## elf_overlay host tools

The build emits the `.elfo` overlay sidecar with two host tools, `elf_overlay_relink` and `elf_overlay_emit`, which must be on `PATH`. Build them from an [elf_overlay](https://github.com/TalismanDesign/elf_overlay) checkout:

```sh
git clone https://github.com/TalismanDesign/elf_overlay
cd elf_overlay
cmake -S . -B build
cmake --build build --target elf_overlay_relink elf_overlay_emit
export PATH="$PWD/build:$PATH"
```

Override the tool paths instead of using `PATH` with `ELF_OVERLAY_RELINK` and `ELF_OVERLAY_EMIT`. The overlay format itself is documented in [overlay.md](../overlay.md).

## pm CLI

The `pm` CLI ships with the SDK as the `pocketmage_sdk` package:

```sh
pip install .        # from the SDK repo root
pm --help
```

`pm` wraps the same `make` contract and adds the enforcing dependency gate. The full command surface is in [cli.md](../cli.md). Icon validation uses Pillow, so install it if you want `pm check` to validate the icon:

```sh
pip install Pillow
```

## Verify

```sh
pm new hello
cd hello
pm build --pack
pm check
```

A green `pm check` means every symbol the app references resolves against the host surface, and the app is ready to install. If any of these steps fail, see [Troubleshooting](../troubleshooting.md).
