# Contributing to PocketMage_SDK

The SDK owns the app-facing contract, so most contributions are small but
ABI-sensitive. Read the docs before touching this repo, in particular
[app-abi.md](docs/docs/app-abi.md) and [symbols.md](docs/docs/symbols.md).

## What the SDK must never break

- The `ET_DYN` format and the `app_main` entry invocation in
  [app-abi.md](docs/docs/app-abi.md).
- The curated export surface. Exports are additive and gated by
  `pocketmage_sdk_version`; removing an exported symbol breaks live apps.
  Keep `symbols.list` and the host-side `host_exports.list` in lockstep, and
  regenerate the host table when either changes.
- The `tools/app.mk` contract: `make`, `make check`, `make pack` must stay
  functional for the example and for app developers. The endianness guard is
  load-bearing; never weaken it.
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
python3 pocketmage_i18n/tools/gen_i18n.py --force \
  --catalog pocketmage_i18n/languages --out-dir /tmp/pm-sdk-i18n
python3 -m unittest discover -s pocketmage_i18n/tests
cd docs && npm install && npm run build               # docs site must build
```

`example-build` and `i18n-generate` in `.github/workflows/sdk.yml` and the
docs build in `.github/workflows/docs.yml` enforce all of the above on every
pull request, on a runner that installs the pinned toolchain itself. A red
SDK CI blocks merging.

## Working with the OS repo

The OS (PocketMage_PDA) vendors this repo as a submodule at
`Code/PocketMageOS/lib/PocketMage_SDK`. When a contract change requires a
host-side change (exports, loader behavior, slots), the OS PR and the SDK PR
travel together and must be merged in dependency order. Bump
`pocketmage_sdk_version` and add a migration note whenever existing apps would
react to the change.