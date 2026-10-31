---
type: concept
title: "Your first app"
description: "Scaffold, build, gate, pack, and install a minimal PocketMage app end to end."
source: "https://talismandesign.github.io/PocketMage_SDK/docs/get-started/first-app/"
path: /get-started/first-app/
updated: 2026-10-31
okf:
  generated_by: "@docmd/plugin-okf"
  generated_at: "2026-10-31T22:45:59.834Z"
---
---
title: "Your first app"
description: "Scaffold, build, gate, pack, and install a minimal PocketMage app end to end."
---

# Your first app

This walks the full path from an empty directory to an app running on the device. It assumes the [toolchain](toolchain.md) is installed.

## 1. Scaffold

```sh
pm new hello
cd hello
```

`pm new` clones the official [PocketMage_App](https://github.com/TalismanDesign/PocketMage_App) template. You get a `Makefile` that includes `tools/app.mk`, a `main.cpp` with an `app_main` entry, an icon, and an `app.properties` manifest.

The entry point is `app_main`, written with C linkage so the mangled name matches the `-e app_main` linker entry. An app calls the flat `pm_*` C API from `pm_app_api.h`, not the C++ singletons; the app links `-nostdlib` and resolves every `pm_*` symbol from the host at load time:

```cpp
#include "pm_app_api.h"

extern "C" int main(int argc, char **argv) {
    const char *name = argc > 0 && argv[0] ? argv[0] : "hello";
    pm_oled_sysmsg(name, 1500);
    return 0;   // returning hands control back to the OS
}
```

The runner passes four arguments: `argv[0]` is the app name, `argv[1]` the absolute ELF path, `argv[2]` the version string, and `argv[3]` the author, all taken from the install record and the manifest. See [app-abi.md](../app-abi.md).

## 2. Build

```sh
pm build --pack
```

This produces `build/hello.app.elf` (the relocatable ELF), `build/hello.elfo` (the overlay sidecar), and `build/hello.tar` (the delivery package). Under the hood it runs `make all pack`; the targets are in [build.md](../build.md).

## 3. Gate

```sh
pm check
```

`pm check` reads the app's undefined-symbol list, which is its real dependency contract because the app links `-nostdlib`, and classifies every symbol against the host surface. A green result means the host resolves everything. For an exact result against a specific firmware, pass the host ELF:

```sh
pm check --host-elf /path/to/firmware.elf
```

The symbol model is in [symbols.md](../symbols.md); the gate's warnings and exit codes are in [cli.md](../cli.md#pm-cli-commands-pm-check-appdir-host-elf-elf-readelf-bin).

## 4. Inspect the package

```sh
tar -tf build/hello.tar
```

It should list `hello.app.elf`, `hello.elfo`, `hello_ICON.bin`, and `app.properties`. The packaging rules are in [publishing](../guides/publishing.md).

## 5. Install on the device

1. Copy `build/hello.tar` onto the SD card at `/apps/hello.tar`, usually over USB.
2. On the device, open the app loader and use the swap action to install the tar into one of the four slots.
3. Launch it from the home screen with `A`, `B`, `C`, or `D`, or from the loader.

Delete the tar from `/apps` after a successful install to free space on the card. The device-side install behavior is owned by the [PocketMageOS docs](https://talismandesign.github.io/PocketMage_PDA/docs/guides/elf-apps/).

## Next

- Understand the whole model: [Making an app](../guides/making-apps.md).
- Draw to the displays: [Rendering](../guides/rendering.md).
- Browse the API: [Function index](../api/functions.md).
