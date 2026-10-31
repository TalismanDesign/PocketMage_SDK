# PocketMage_SDK

The official SDK for building PocketMage apps. An app ships as an ELF the OS loads from the SD card at runtime and runs in-process. There is no firmware reflash. This repo defines the contract: the build rules, the curated symbol surface, the overlay format, the translation engine, and the `pm` CLI.

## Repositories

- **PocketMage_SDK** (this repo): the app-facing contract, build tools, the base i18n catalog, and the `pm` CLI.
- **PocketMage_App**: the official app template `pm new` clones. CI builds it against this SDK on every push.
- **elf_overlay**: the ISA-abstracted overlay runtime and the host tools (`elf_overlay_relink`, `elf_overlay_emit`) that emit the `.elfo` sidecar.
- **PocketMage_PDA**: the OS. Ships the firmware, the ELF loader (`esp_elf` + `elf_overlay`), slot management, and the APPLOADER installer. Vendors this repo as a submodule at `Code/PocketMageOS/lib/PocketMage_SDK`.

## Quick start

Install the CLI and scaffold an app from the template:

```sh
pip install .            # installs the `pm` CLI
pm new hello
cd hello
pm build --pack          # build/hello.app.elf, build/hello.elfo, build/hello.tar
pm check                 # gate the app's undefined symbols against the host surface
```

Drop `build/hello.tar` in `/apps/` on the device SD card and install it through APPLOADER. The build needs the pinned espressif Xtensa toolchain and the `elf_overlay` host tools on `PATH`; see the [toolchain guide](Docs/docs/get-started/toolchain.md).

`pm check --host-elf firmware.elf` is the exact dependency gate; without a host ELF it checks against the curated surface. `pm exports` reconciles the SDK's surface with a host checkout's `host_exports.list`, and `pm release <major|minor|patch>` bumps the version across all four version lands at once. The full CLI surface is in [cli.md](Docs/docs/cli.md).

## Documentation

The SDK ships its own DocMD site (`Docs/`): build it with `cd Docs && npm install && npm run build` and preview `Docs/site/`. Published at `https://talismandesign.github.io/PocketMage_SDK/docs`. Entry points:

| Doc                                         | Covers                                                        |
| ------------------------------------------- | ------------------------------------------------------------- |
| [Get Started](Docs/docs/get-started/index.md) | Toolchain install and a first app end to end                |
| [API Reference](Docs/docs/api/index.md)     | Every component, the `pm_*` C ABI, and the projection model   |
| [App ABI](Docs/docs/app-abi.md)             | The ELF binary contract and the two-stage load                |
| [Overlay format](Docs/docs/overlay.md)      | The `.elfo` sidecar that pages app code into executable RAM   |
| [Building](Docs/docs/build.md)              | Toolchain, project layout, `make` targets                     |
| [Symbols](Docs/docs/symbols.md)             | The curated export surface and the dependency contract        |
| [Publishing](Docs/docs/guides/publishing.md) | Packaging the delivery tar and what the installer expects    |
| [i18n](Docs/docs/i18n.md)                   | Translation catalogs, the generator, and string ownership     |
| [Migration](Docs/docs/migration.md)         | Moving an existing app to the ELF flow                        |

## Layout

```text
PocketMage_SDK/
  tools/
    app.mk            # compile, strip, check, overlay-emit, pack recipe
    gen_app_api.py    # projects the C++ API to the pm_* C ABI (symbols.list)
    symbols.py        # curated host-export table helper
    pm/               # the pm CLI (pyproject-driven), with tests/fixtures/
  pyproject.toml      # pip install ., entry point `pm`
  symbols.list        # curated host-export surface the SDK commits to
  host_baseline.list  # the C/C++ runtime + POSIX symbols the host always exports
  pocketmage_*/       # the SDK modules: eink, oled, sd, kb, touch, clock, bz,
                      # wifi, font, layout, ui, i18n, sys, globals
  pocketmage.h        # umbrella header an app includes first
  pocketmage_i18n/    # translation engine, base catalog, generator
  docs/               # DocMD docs site (config, pages, lockfile)
  .github/workflows/  # SDK CI (app-template build, version sync, i18n,
                      # lint, pm CLI tests, docs deploy)
```

The SDK has no `examples/` directory. The runnable reference app is the external [PocketMage_App](https://github.com/TalismanDesign/PocketMage_App) template; CI checks it out and builds it against the SDK on every push. Test fixtures (a negative `broken_app`, a golden `hello_app.app.elf`, a callgraph case, and a scaffold template) live under `tools/pm/tests/fixtures/`.

## CI

`.github/workflows/sdk.yml` runs five jobs on a fresh runner that installs the pinned espressif Xtensa toolchain itself:

- **app-template** - checks out `PocketMage_App` and `elf_overlay`, builds the overlay host tools, builds and packs the template against this SDK, asserts the artifact is little-endian with a 200-byte icon and that the tar carries the ELF, and gates it with `pm check`.
- **version-sync** - asserts `VERSION`, `pocketmage_sdk_version` in `pocketmage_globals.cpp`, the macros in `pocketmage_app_version.h`, and `library.json` all agree.
- **i18n-generate** - runs `gen_i18n.py` and the i18n unit tests.
- **lint** - `ruff` on the Python and `clang-format` on the C++.
- **pm-cli** - runs the pm CLI test suite (parsing, gate, release, golden) on Linux, macOS, and Windows.

`.github/workflows/docs.yml` builds the DocMD site and deploys it to GitHub Pages. Keep both green.

## Contributing

See [CONTRIBUTING.md](CONTRIBUTING.md). The short version: never break the binary contract, keep every exported symbol curated and accounted for, and leave CI green.
