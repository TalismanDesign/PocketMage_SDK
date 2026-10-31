---
type: concept
title: Glossary
description: "The terms the SDK docs use, defined once, with links to the page that owns each concept."
source: "https://talismandesign.github.io/PocketMage_SDK/docs/glossary/"
path: /glossary/
updated: 2026-10-31
okf:
  generated_by: "@docmd/plugin-okf"
  generated_at: "2026-10-31T22:45:59.835Z"
---
---
title: "Glossary"
description: "The terms the SDK docs use, defined once, with links to the page that owns each concept."
---

# Glossary

The terms below mean exactly one thing across these docs. Where a concept has its own page, the definition links to it.

**App.** A standalone 32-bit `ET_DYN` ELF the host loads and runs in-process at runtime. Links `-nostdlib`; resolves everything from the host. See [App ABI](app-abi.md).

**`app_main`.** The app entry point. Written as `main` with C linkage; the build renames it to `app_main` with `-Dmain=app_main -e app_main`. Runs on core 1 in a 16 KB task. See [App ABI](app-abi.md#app-binary-contract-abi-entry-point).

**`app.properties`.** The optional per-app manifest: `name`, `version`, `author`, and `scope`. Shipped in the tar and copied to the slot; the first three reach `app_main` as `argv`. See [App ABI](app-abi.md) and [per-app scope](symbols.md#symbols-and-the-dependency-contract-per-app-scope).

**APPLOADER.** The on-device installer. Extracts a `/apps/*.tar`, finds the `*.app.elf`, and installs it with its icon and manifest into one of four slots. See [publishing](guides/publishing.md).

**Curated surface.** The union of `symbols.list` and `host_baseline.list`: the set of symbols an app may resolve against. See [symbols.md](symbols.md).

**ELFO / `.elfo`.** The overlay blob that ships beside each app ELF. Magic `ELFO`, ISA-tagged, carrying overlay bodies, stubs, and relocation patch tables. See [overlay.md](overlay.md).

**`elf_overlay`.** The ISA-abstracted overlay runtime that pages app `.text` bodies into executable RAM and patches ISA-specific literal loads at page-in. Sits next to `esp_elf`. See [overlay.md](overlay.md).

**`esp_elf`.** The base ELF relocator. Relocates the app image streamed from SD, then hands off to `elf_overlay` for the overlay pass. See [App ABI](app-abi.md#app-binary-contract-abi-how-loading-works).

**`gen_app_api.py`.** The generator that walks the SDK headers and projects each C++ method to a `pm_*` C wrapper, writing the names into `symbols.list` and the host `host_exports.list`. See [symbols.md](symbols.md).

**Host.** The PocketMageOS firmware. It owns the hardware, exports the symbol surface, and runs apps in-process. The device side is documented in the [PocketMageOS docs](https://talismandesign.github.io/PocketMage_PDA/docs).

**`host_baseline.list`.** The committed list of C/C++ runtime, newlib, and POSIX symbols the firmware exports unconditionally. Generated from the firmware export table; part of the curated surface. See [symbols.md](symbols.md).

**ISA.** Instruction set architecture. PocketMage runs on Xtensa (ESP32-S3); `elf_overlay` also has a RISC-V backend. The ELFO header is ISA-tagged and a blob loads only on its ISA. See [overlay.md](overlay.md#overlay-format-elfo-isa-abstraction).

**`MALLOC_CAP_EXEC`.** Heap capability for memory the instruction bus can fetch. Relocated and paged-in `.text` lands here; it is scarce, which is why the overlay model pages bodies in on demand. Code in PSRAM is not executable on the S3.

**Overlay body.** One `.text` image paged into an exec-RAM frame on demand. A body must fit in a single frame. See [overlay.md](overlay.md#overlay-format-elfo-how-the-host-consumes-it-the-frame-pool).

**`pm_*` ABI.** The flat C ABI an app calls, generated from the SDK's C++ classes. Declared in `pm_sdk_app.h`, pulled in through `pm_app_api.h`. See the [function index](api/functions.md).

**`PM_APP_API_ABI`.** The ABI version an app compares against `pm_app_abi()` at startup to confirm it was built against a compatible surface.

**`pocketmage_sdk_version`.** The version string the host exports and apps read to check compatibility at runtime. Bumped by `pm release`.

**Scope.** The `app.properties` field that restricts which `pm_*` symbols an app may resolve (default `all`). An out-of-scope `pm_*` reference fails the load. See [per-app scope](symbols.md#symbols-and-the-dependency-contract-per-app-scope).

**Slot.** One of four install locations on the SD card (`/apps/slot1` through `/apps/slot4`), tracked in NVS. Launched from HOME with `A`-`D`.

**String pool.** The rotating buffer backing `pm_*` `const char*` returns: four 256-byte slots, the fifth string-returning call overwrites the first. Copy a returned string before further SDK calls. See [function index](api/functions.md#function-index-string-return-lifetime).

**Stub.** The small entry/leave code in the ELFO blob that pages an overlay body in on first call and drops its frame refcount on return. See [overlay.md](overlay.md).

**Target.** The compile mode, exactly one of `PM_TARGET_HOST` (firmware) or `PM_TARGET_APP` (external app), enforced by `config.h`. See [API overview](api/index.md#api-reference-compile-target).
