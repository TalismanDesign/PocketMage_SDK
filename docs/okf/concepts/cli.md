---
type: concept
title: "pm CLI"
description: "The pm developer tool: scaffolding, building, gating, packing, exports reconciliation, and release bumps."
source: "https://talismandesign.github.io/PocketMage_SDK/docs/cli/"
path: /cli/
updated: 2026-10-31
okf:
  generated_by: "@docmd/plugin-okf"
  generated_at: "2026-10-31T22:45:59.834Z"
---
---
title: "pm CLI"
description: "The pm developer tool: scaffolding, building, gating, packing, exports reconciliation, and release bumps."
---

# `pm` CLI

`pm` is the developer-facing wrapper around the SDK build. It drives the same `make` contract documented in [build.md](build.md), adds the enforcing dependency gate, and handles scaffolding and release bookkeeping. Everything `pm` does can be done with `make` and the raw tools; `pm` is the ergonomic path and is what CI runs.

## Install

The CLI ships with the SDK as the `pocketmage_sdk` package:

```sh
pip install .        # from the SDK repo root
pm --help
```

It shells out to `make` and the Xtensa toolchain, so an app build still needs the pinned espressif toolchain on the machine (see [get-started/toolchain](get-started/toolchain.md)).

## Global options

| Option          | Effect                                                   |
| --------------- | -------------------------------------------------------- |
| `--ci`          | plain output, no ANSI escapes                            |
| `--no-color`    | alias for `--ci`                                         |
| `--theme NAME`  | output theme: `default`, `mono`, or `pastel`             |

CI mode also turns on automatically when `CI` or `NO_COLOR` is set in the environment.

## Commands

### `pm new <name> [dest]`

Scaffolds a new app from the official [PocketMage_App](https://github.com/TalismanDesign/PocketMage_App) template into `dest` (default: the current directory). Replace the scaffolded `assets/icon.png` with your own 40x40 artwork to change the APPLOADER card icon. The next steps are `pm build --pack` then `pm check`.

### `pm build [app_dir] [--clean] [--pack]`

Builds the app via `make`. `--clean` runs `make clean` first; `--pack` also builds the delivery tar. Requires the Xtensa toolchain to be present and resolves the same paths `make pm-info` prints. Produces `build/<name>.app.elf` and `build/<name>.elfo`.

### `pm check [app_dir] [--host-elf ELF] [--readelf BIN]`

The enforcing dependency gate. Reads the built ELF's undefined-symbol list and classifies each symbol against the curated host surface (see [symbols.md](symbols.md)):

- **resolvable** - in the curated surface (`symbols.list` plus `host_baseline.list`). Passes.
- **unconfirmed** - outside the curated surface. Needs `--host-elf <firmware.elf>` to confirm against the actual host export table; without it, `check` reports the app as unconfirmed and exits non-zero.
- **broken** - not resolvable by the host. The load would fail. Exits non-zero.

`check` also surfaces two warnings that do not fail the gate on their own:

- `app_main` not present in the shipped ELF's symbol table. The entry is still trusted because it is linker-pinned with `-e app_main`; a post-link strip removed the name.
- A non-empty `.init_array`. The app links static constructors, and the host's `esp_elf` loader does not run them. Do not rely on static construction at load time.

The icon is validated as part of the gate; an invalid icon fails the check.

### `pm pack [app_dir]`

Builds the delivery tar via `make pack`. The tar holds `<name>.app.elf`, `<name>.elfo`, `<name>_ICON.bin`, and `app.properties` when present. See [publishing](guides/publishing.md).

### `pm exports [--host LIST] [--all]`

Reconciles the SDK's `symbols.list` against a PocketMageOS `src/ELF_SYMS/host_exports.list`. Every curated symbol must be exported by the host; a curated symbol the host does not export is an error to fix on the OS side, not in `symbols.list`. `--host` points at a specific host list (default: a sibling PocketMageOS checkout). `--all` additionally lists host exports that sit outside the curated surface, as advisory output.

### `pm release {major|minor|patch} [--message TEXT]`

Bumps the SDK release version. Updates `VERSION`, `pocketmage_globals.cpp`, `library.json`, and `CHANGELOG.md`; `--message` files a feature line under the new version in the changelog. It does not commit or tag; commit the bump and tag the release yourself when ready.

### `pm version`

Prints `pocketmage_sdk_version`, the version string the host exports and apps compare against at startup.

## Relationship to make

`pm build`, `pm pack`, and `pm check` run the `make` targets in [build.md](build.md). `pm` reads the resolved toolchain and paths through `make pm-info`, so its view of the build matches `make`'s exactly. When a build behaves differently under `pm` than under `make`, compare `make pm-info` output first.
