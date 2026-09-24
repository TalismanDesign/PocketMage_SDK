# Contributing to PocketMage_SDK

The SDK owns the app-facing contract, so most contributions are small but
ABI-sensitive. Read the docs before touching this repo, in particular
[app-abi.md](docs/docs/app-abi.md) and [symbols.md](docs/docs/symbols.md).

## What the SDK must never break

- The `ET_DYN` format and the `app_main` entry invocation in
  [app-abi.md](docs/docs/app-abi.md).
- The curated export surface. Exports are additive and gated by
  `pocketmage_sdk_version`; removing an exported symbol breaks live apps.
  Keep `symbols.list` and the host-side `host_exports.list` in sync, and
  regenerate the host table when either changes.
- The `tools/app.mk` contract: `make`, `make check`, `make pack`, `make
  pm-info` must stay functional for the example and for app developers. The
  endianness guard is load-bearing; never weaken it. `make check` is
  informational by design; the enforced gate is `pm check`, which the CI
  example-build job runs. The pm CLI replays the make contract through
  `pm-info`, so its parse shape is part of the surface.
- The version: `VERSION`, the `pocketmage_sdk_version` literal in
  `pocketmage_globals.cpp`, the macros in `pocketmage_app_version.h`, and
  `library.json` must agree. The CI `version-sync` job enforces it; use
  `pm release <major|minor|patch>` to bump all four at once.
- The delivery format in [publish.md](docs/docs/publish.md): top-level
  `<name>.app.elf` and `<name>_ICON.bin` inside a plain tar.

## Guidelines

- Match the existing style: terse, plain-ASCII, no decorative formatting.
  Ship no magic numbers where a named constant or comment is clearer.
- A change to i18n behavior touches the base catalog; follow the ownership
  and validation rules in [docs/docs/i18n.md](docs/docs/i18n.md).
- Docs changes update the matching page under `docs/docs/` (the DocMD site),
  the `docs/` build state, and the docs table in `README.md`.
- Unit-testable logic (the i18n generator is the model) gets a `tests/`
  module and a deterministic unittest; run it before pushing.
- No commented-out code, no placeholders, no "TODO later" in shipped work.

## Verification gate

Before you push, run the same thing CI runs:

```sh
make -C examples/hello_app clean all check pack       # example + endianness gate
make -C examples/version_app clean all                # ABI-stamp example builds
make -C examples/broken_app clean all                 # negative fixture builds
python3 -m tools.pm check examples/hello_app          # export-surface gate
python3 -m tools.pm check examples/version_app
if python3 -m tools.pm check examples/broken_app; then # must be rejected
  echo "broken_app must fail pm check" >&2
  exit 1
fi
python3 -m tools.pm exports        # symbols.list vs host_exports.list (needs an OS checkout)
python3 pocketmage_i18n/tools/gen_i18n.py --force \
  --catalog pocketmage_i18n/languages --out-dir /tmp/pm-sdk-i18n
python3 -m unittest discover -s pocketmage_i18n/tests
python3 -m unittest discover -s tools/pm/tests         # pm CLI parsing, gate, release, golden
ruff check tools/                                      # python style (pip install .[dev])
clang-format --dry-run --Werror \
  pocketmage_globals.cpp pocketmage_app_version.h \
  examples/hello_app/main.cpp examples/version_app/main.cpp \
  examples/broken_app/main.cpp
cd docs && npm install && npm run build               # docs site must build
```

The golden test at `tools/pm/tests/test_golden.py` compares a fresh
`hello_app` artifact against the committed fixture
`tools/pm/tests/fixtures/hello_app.app.elf`. When `app.mk` changes the ELF
shape, rebuild hello_app and copy the new binary over the fixture in the same
change: `cp examples/hello_app/build/hello_app.app.elf tools/pm/tests/fixtures/`.
`pm check`, the golden test, and `pm exports` are enforced by `pm-cli` and
`example-build`; see the next section.

`example-build`, `i18n-generate`, `lint`, and `pm-cli` in
`.github/workflows/sdk.yml` and the docs build in
`.github/workflows/docs.yml` enforce all of the above on every pull request,
on a runner that installs the pinned toolchain itself. A red SDK CI blocks
merging.

## Working with the OS repo

The OS (PocketMage_PDA) vendors this repo as a submodule at
`Code/PocketMageOS/lib/PocketMage_SDK`. When a contract change requires a
host-side change (exports, loader behavior, slots), the OS PR and the SDK PR
travel together and must be merged in dependency order. Bump
`pocketmage_sdk_version` and add a migration note whenever existing apps would
react to the change.