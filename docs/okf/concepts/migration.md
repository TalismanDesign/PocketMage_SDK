---
type: concept
title: "Migrating an existing app"
description: "Moving a pre-ELF app to the runtime-load flow: what changed, the eight steps, and the traps that bite."
source: "https://talismandesign.github.io/PocketMage_SDK/docs/migration/"
path: /migration/
updated: 2026-09-24
okf:
  generated_by: "@docmd/plugin-okf"
  generated_at: "2026-09-24T05:42:26.157Z"
---
---
title: "Migrating an existing app"
description: "Moving a pre-ELF app to the runtime-load flow: what changed, the eight steps, and the traps that bite."
---

# Migrating an existing app

Production devices no longer run apps built the old way. An app is now a
runtime-loaded ELF shipped as `/apps/*.tar`; there is no firmware-flashing
app flow and no OTA-style app package on production.

The short version: if you shipped an app pre-ELF, that package will not launch
on production firmware. Rebuild it against the ELF contract and re-package.

## What changed

| Aspect          | Old flow                                   | New flow                                             |
| --------------- | ------------------------------------------ | ---------------------------------------------------- |
| Binary          | monolithic app inside the firmware image   | standalone `ET_DYN` ELF                            |
| Delivery        | firmware `.bin` / OTA-style app package    | `/apps/<name>.tar` on the SD card, installed via APPLOADER |
| Instantiation   | compiled in, launched by the OS            | loaded in-process when launched, torn down on return |
| Dependencies    | linked into the firmware build             | resolved from the host at load time (`-nostdlib`)    |
| Toolchain       | PlatformIO (big-endian)                    | espressif `xtensa-esp-elf`                          |
| Layout/naming   | app id / metadata                          | derived from the tar base name                       |

The OS keeps app code out of PSRAM by design; code runs from IRAM. That is
the loader's job, not yours.

## Migration steps

1. **Get the toolchain.** Install the espressif `xtensa-esp-elf` toolchain
   into `~/.espressif/...` as shown in [build.md](build.md). Do not rely on
   the PlatformIO toolchain; it emits big-endian ELFs the loader cannot read.
2. **Scaffold.** Copy `examples/hello_app` as a starting point. Its
   `Makefile` (`include tools/app.mk`) is the whole build system.
3. **Port the entry.** Wrap your start logic in `extern "C" int
   main(int, char**)`. The build renames it to `app_main`. Fit the previous
   app's lifetime to the return-from-`main` model
   ([app-abi.md](app-abi.md)).
4. **Replace dependencies.** Every call must resolve at load time. Host
   exports are the curated surface in [symbols.md](symbols.md) plus the
   host's newlib/IDF/Arduino globals. That covers display, keyboard, buzzer,
   clock, IO, and the standard library. If you depended on app-side firmware
   globals that are not exported, move that dependency to API calls.
5. **Move assets.** Files the app used at compile time become a top-level
   `assets/` directory in the tar, read at runtime from `/assets/<name>`
   ([publish.md](publish.md)).
6. **Localize.** If the app contained strings, move them into an app catalog
   (`src/i18n/`, merged per [i18n.md](i18n.md)) instead of hardcoding.
7. **Verify.** `make clean all check` must succeed, list a sane set of
   undefined symbols, and the endianness check must pass. Then `make pack`
   and `tar -tvf build/<name>.tar`.
8. **Ship.** Copy the tar to `/apps/` on the device, install with APPLOADER
   into a slot, launch. The launcher shows the name and icon drawn from the
   package.

## Gotchas

- **Big-endian artifacts.** The classic failure. The loader does no byte
  swapping, so a PlatformIO-built app faults at load with a section or
  instruction fault. `readelf -h` shows `big endian` before you ship.
- **Statically linked runtime.** Symbols collide or bloat; keep `-nostdlib`.
- **Relying on slot directories.** After install the ELF lives in the slot
  dir, but the app should treat `/assets/<name>` (copied at install) as its
  data home and never assume the slot path.
- **Long-lived loops.** The app shares the device. Cooperative lifetimes
  (render, yield via the SDK API, return) beat spin loops on the 16 KB run
  stack.

## Announcement

If you maintained published apps pre-ELF, publish a migration notice: existing
packages targeting production will not launch on the new firmware, and step 8
above is the path forward.
