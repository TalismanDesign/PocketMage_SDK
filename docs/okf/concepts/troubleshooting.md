---
type: concept
title: Troubleshooting
description: "The failures an app build or load hits most, what causes each, and how to fix it."
source: "https://talismandesign.github.io/PocketMage_SDK/docs/troubleshooting/"
path: /troubleshooting/
updated: 2026-11-01
okf:
  generated_by: "@docmd/plugin-okf"
  generated_at: "2026-11-01T00:02:39.183Z"
---
---
title: "Troubleshooting"
description: "The failures an app build or load hits most, what causes each, and how to fix it."
---

# Troubleshooting

The failure modes below are the ones an app build or load actually hits. Each entry names the symptom, the cause, and the fix. The deeper references are linked from each.

## Build and toolchain

### `XTENSA_CXX` not found, or the wrong compiler is used

The build probes for the espressif `xtensa-esp32s3-elf-g++` and falls back to the PlatformIO package and `PATH`. If it picks the PlatformIO package, the link output has the wrong byte order and the endianness guard deletes it (see below). Pin the compiler explicitly:

```sh
make XTENSA_CXX=~/.espressif/tools/xtensa-esp-elf/esp-15.2.0_20251204/xtensa-esp-elf/bin/xtensa-esp32s3-elf-g++
```

Confirm what the build resolved with `make pm-info`. The install steps are in [toolchain.md](get-started/toolchain.md).

### The build fails with an endianness assertion and deletes the artifact

`app.mk` asserts little-endian output after every link and removes a big-endian artifact so a bad file never ships. This almost always means the wrong toolchain compiled the app. Only the espressif `xtensa-esp-elf` toolchain emits readable output; the PlatformIO Xtensa package does not. Fix the toolchain, not the guard.

### `elf_overlay_relink` or `elf_overlay_emit` not found

The `.elfo` overlay sidecar is built by two host tools that must be on `PATH`. Build them from an `elf_overlay` checkout and add the build directory to `PATH`, or point `ELF_OVERLAY_RELINK` and `ELF_OVERLAY_EMIT` at the binaries. See [toolchain.md](get-started/toolchain.md#install-the-toolchain-elfoverlay-host-tools).

### A single function will not fit in an overlay frame

An overlay body must fit in one frame of the executable-RAM pool (`ELF_OVERLAY_DEFAULT_FRAME_SIZE`, default 16 KB). A single function larger than a frame cannot be paged in. Split the function. See [overlay.md](overlay.md#overlay-format-elfo-how-the-host-consumes-it-the-frame-pool).

## Gating (`pm check`)

### Symbols reported as "broken" / "not resolvable by the host"

The app references a symbol the host does not export. The load would fail. The symbol must be added to the SDK's curated surface before an app can use it, or the app must stop referencing it. The symbol model is in [symbols.md](symbols.md).

### Symbols reported as "unconfirmed"

The symbol sits outside the curated surface (`symbols.list` plus `host_baseline.list`), so `pm check` cannot confirm it without the actual host export table. Re-run with `--host-elf <firmware.elf>` for an exact result.

### Warning: `app_main` is not in the ELF's symbol table

Not a failure. The post-link strip removed the `app_main` name, but the entry is linker-pinned with `-e app_main`, so the loader still finds it. Ignore it.

### Warning: the app has a non-empty `.init_array`

The app links static constructors. The host's `esp_elf` loader does not run relocation-time constructors, so they never execute. Remove the static construction, or move the initialization into `app_main`. The compile flags `-fno-exceptions -fno-rtti -fno-threadsafe-statics` are on by default to keep this from happening; a remaining `.init_array` usually comes from a global with a non-trivial constructor. See [app-abi.md](app-abi.md#app-binary-contract-abi-how-loading-works).

## pm tooling

### `ModuleNotFoundError: No module named 'tools'` when running `pm`

The `pm` command was installed editable (`pip install -e`) against a checkout that has since moved or been deleted, so its link to the `tools` package is dead. A common trigger is installing from a temporary scaffold directory that the OS later cleaned up. Current `pm` builds detect this and offer to reinstall; if you see the raw error, the installed CLI predates the resilient launcher. Reinstall against a live checkout:

```sh
pm update-sdk --sdk /path/to/PocketMage_SDK
```

If `pm` itself will not start, run the equivalent reinstall directly with the interpreter that owns the `pm` script:

```sh
/path/to/that/python -m pip install -e /path/to/PocketMage_SDK
```

Run [`pm doctor`](cli.md#pm-cli-commands-pm-doctor) afterward to confirm the install is healthy.

### `pm` behaves differently from the SDK checkout you are editing

The installed CLI is a different version from the checkout. `pm doctor` reports the version skew. Reinstall to re-sync:

```sh
pm update-sdk
```

An editable install avoids this in the first place, because the CLI tracks the checkout it was linked to. See [the install notes](cli.md#pm-cli-install).

## Load-time (on device)

### `ELF needs symbols outside its scope`

The app's `app.properties` manifest declares a `scope` that does not include a `pm_*` symbol the app actually calls, so the scoped resolver refuses to resolve it and the load fails fast. Widen the `scope` (default `all`) to cover the API the app uses, or stop calling the out-of-scope API. Only `pm_*` symbols are gated by scope; the runtime baseline is always reachable. See [symbols.md](symbols.md#symbols-and-the-dependency-contract-per-app-scope).

### The app loads but runs unredirected / behaves oddly on large code paths

The `.elfo` sidecar was not found next to the ELF, so the app ran from the base relocation alone without the overlay. Confirm the tar shipped the `.elfo` member (`tar -tf build/<name>.tar`) and that the installer placed it in the slot. See [overlay.md](overlay.md#overlay-format-elfo-how-the-host-consumes-it).

### The app never returns control

Returning from `app_main` is how an app exits: the overlay is unloaded, the base image deinitialized, and HOME reopens. An app that never returns leaks the run task. A UI app must hand control back cooperatively and then return. See [app-abi.md](app-abi.md#app-binary-contract-abi-entry-point).

## Packaging and install

### The device will not install the tar

The installer only accepts a plain `.tar` in `/apps`, not a gzip, not a loose directory, not a `.bin`. It keys off the first `*.app.elf` member. Rebuild with `make pack` (or `pm pack`), which produces the correct shape, and verify with `tar -tvf`. See [publishing](guides/publishing.md).

### macOS `._*` junk in the package

Creating the tar from Finder copies pulls in `._*` metadata files. The installer skips them, but they bloat the package. `make pack` avoids them; use it rather than tarring by hand.
