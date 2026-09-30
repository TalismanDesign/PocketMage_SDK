---
title: "Making an app"
description: "Scaffold, build, pack, and run your first PocketMage app."
---

# Making an app

A PocketMage app is a position-independent xtensa ELF that the host firmware loads into RAM and runs in-process. It links nothing: newlib, libstdc++, and the SDK itself are all left undefined and resolved at load time against the host's export table (see [the ABI](app-abi.md) and [symbols](../symbols.md)).

## 1. Scaffold

```sh
pm new myapp
```

This copies `examples/hello_app` into `./myapp`:

- `main.cpp`, the app source.
- `myapp_ICON.bin`, the 1-bit app icon (128x128, big-endian row-major) bundled into the delivery tar.
- `Makefile`, a two-line file that sets an absolute `SDK_ROOT` and includes the SDK recipe.

App names are `[A-Za-z][A-Za-z0-9_]*`; the name becomes the ELF, tar, icon, and install slot name. To start from an API walkthrough instead, open the [rendering guide](rendering.md) or the [version example](https://github.com/TalismanDesign/PocketMage_SDK/blob/main/examples/version_app/main.cpp).

## 2. Write code

```cpp
#include <stdio.h>
#include <unistd.h>

extern "C" int main(int argc, char **argv) {
  (void)argc;
  (void)argv;
  printf("hello from pocketmage sdk app\n");
  for (int i = 0; i < 3; ++i) {
    printf("tick %d\n", i);
    sleep(1);
  }
  printf("app done\n");
  return 0;
}
```

That is the whole hello app: `main` with C linkage (the ABI entry point), libc working (`printf`, `sleep`), and a plain `return` to exit. Drop in `#include <pocketmage.h>` and the `PM_SD()`/`OLED()`/etc. singletons and you have a UI app.

The SDK umbrella headers are only ever compiled as *declarations* inside an app; the implementations live in the host. `PM_SDK_VERSION*` macros are injected by the build so an app can compare against the running host's `pocketmage_sdk_version` export.

## 3. Build

```sh
pm build myapp            # runs make; == the raw `make` path below
```

or directly:

```sh
make                      # uses SDK_ROOT from the scaffold Makefile
```

The recipe (`tools/app.mk`) compiles with `-fPIC -fno-exceptions -fno-rtti`, hides all visibility, link-strips the artifact, and verifies it is a little-endian ELF (the loader rejects big-endian targets). Output is `build/<name>.app.elf`.

Useful override variables: `APP_SRCS` (default `main.cpp`), `APP_NAME`, `APP_BUILD_DIR`. The `pm-info` target prints everything the tooling needs.

## 4. Check the ABI surface

```sh
pm check myapp
```

Lists the app's undefined GLOBAL symbols, which is exactly what the host must resolve. Keep that list inside the host export table ([`symbols.list`](../symbols.md)); `pm exports` diffs the list against the host export table so the two cannot drift.

## 5. Pack

```sh
pm build myapp --pack     # build + pack
# or:  pm pack myapp
# or:  make pack
```

Produces `build/<name>.tar` containing the stripped ELF and the icon. That tar is what gets installed onto the SD card (see [publishing](../publish.md)).

## 6. Run

The OS's APPLOADER installs the app and its icon, and menus it on HOME. Launching the app runs `main` inside the host process; see [lifecycle](lifecycle.md) for what happens at start and exit, and
