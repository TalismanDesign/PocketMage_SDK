# Contributing to PocketMage_SDK

The SDK owns the app-facing contract, so most contributions are small but ABI-sensitive. Read the docs before touching this repo, in particular [app-abi.md](Docs/docs/app-abi.md) and [symbols.md](Docs/docs/symbols.md).

## What the SDK must never break

- The `ET_DYN` format and the `app_main` entry invocation in [app-abi.md](Docs/docs/app-abi.md).
- The curated export surface. Exports are additive and gated by `pocketmage_sdk_version`; removing an exported symbol breaks live apps. Keep `symbols.list` and the host-side `host_exports.list` in sync, and regenerate the host table when either changes.
- The `tools/app.mk` contract: `make`, `make check`, `make pack`, `make pm-info` must stay functional for the template and for app developers. The endianness guard is load-bearing; never weaken it. `make check` is informational by design; the enforced gate is `pm check`, which the CI `app-template` job runs. The pm CLI replays the make contract through `pm-info`, so its parse shape is part of the surface.
- The version: `VERSION`, the `pocketmage_sdk_version` literal in `pocketmage_globals.cpp`, the macros in `pocketmage_app_version.h`, and `library.json` must agree. The CI `version-sync` job enforces it; use `pm release <major|minor|patch>` to bump all four at once.
- The delivery format in [publishing](Docs/docs/guides/publishing.md): top-level `<name>.app.elf` and its `<name>.elfo` overlay sidecar, plus the optional `<name>_ICON.bin`, inside a plain tar.
- The overlay format in [overlay.md](Docs/docs/overlay.md): the `.elfo` blob the host pages into executable RAM. The SDK emits it with the `elf_overlay` host tools; its layout is part of the contract.

## Guidelines

- Match the existing style: terse, plain-ASCII, no decorative formatting. Ship no magic numbers where a named constant or comment is clearer.
- A change to i18n behavior touches the base catalog; follow the ownership and validation rules in [Docs/docs/i18n.md](Docs/docs/i18n.md).
- Docs changes update the matching page under `Docs/docs/` (the DocMD site), the `Docs/` build state, and the docs table in `README.md`.
- Unit-testable logic (the i18n generator is the model) gets a `tests/` module and a deterministic unittest; run it before pushing.
- No commented-out code, no placeholders, no "TODO later" in shipped work.

## Verification gate

The SDK has no `examples/` directory. The runnable reference app is the external [PocketMage_App](https://github.com/TalismanDesign/PocketMage_App) template; CI checks it out and builds it against your branch. Before you push, run the same checks CI runs. The app build steps need the pinned espressif Xtensa toolchain and the `elf_overlay` host tools on `PATH`.

Build and gate the template against your branch:

```sh
pip install .                                          # the pm CLI, from this repo
pm new /tmp/pm-check-app                               # scaffold from the template
pm build --clean --pack /tmp/pm-check-app              # build + endianness gate + .elfo + tar
pm check /tmp/pm-check-app                             # export-surface gate
```

Run the local test suites and style checks CI enforces:

```sh
python3 pocketmage_i18n/tools/gen_i18n.py --force \
  --catalog pocketmage_i18n/languages --out-dir /tmp/pm-sdk-i18n
python3 -m unittest discover -s pocketmage_i18n/tests  # i18n generator
python3 -m unittest discover -s tools/pm/tests         # pm CLI parsing, gate, release, golden
python3 -m tools.pm exports                            # symbols.list vs host_exports.list (needs an OS checkout)
ruff check tools/                                      # python style (pip install .[dev])
clang-format --dry-run --Werror \
  pocketmage_globals.cpp pocketmage_app_version.h
cd Docs && npm install && npm run build                # docs site must build
```

The golden test at `tools/pm/tests/test_golden.py` validates the committed fixture `tools/pm/tests/fixtures/hello_app.app.elf` through the gate (header, endianness, undefined-symbol classification). When `app.mk` changes the ELF shape, rebuild a `hello_app`-named app and copy the new binary over the fixture in the same change: `cp build/hello_app.app.elf tools/pm/tests/fixtures/`. The `broken_app` fixture is the negative case the gate must reject.

The `app-template`, `version-sync`, `i18n-generate`, `lint`, and `pm-cli` jobs in `.github/workflows/sdk.yml` and the docs build in `.github/workflows/docs.yml` enforce all of the above on every pull request, on a runner that installs the pinned toolchain itself. A red SDK CI blocks merging.

## Working with the OS repo

The OS (PocketMage_PDA) vendors this repo as a submodule at `Code/PocketMageOS/lib/PocketMage_SDK`. When a contract change requires a host-side change (exports, loader behavior, slots), the OS PR and the SDK PR travel together and must be merged in dependency order. Bump `pocketmage_sdk_version` and add a migration note whenever existing apps would react to the change.
