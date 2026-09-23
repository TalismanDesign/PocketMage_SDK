# PocketMage_SDK

The official SDK for building PocketMage apps. Apps ship as ELF files the OS
loads at runtime and runs in-process. This repo defines the contract: the
build rules, the curated symbol surface, the translation engine, and the
reference example.

## Repositories

- **PocketMage_SDK** (this repo): app-facing contract, build tools, base i18n
  catalog, examples.
- **PocketMage_PDA**: the OS. Ships the firmware, the ELF loader, slot
  management, and the APPLOADER installer. Contains this repo as submodule at
  `Code/PocketMageOS/lib/PocketMage_SDK`.

## Quick start

```sh
make -C examples/hello_app clean all check pack
```

That builds `hello_app.app.elf` with the espressif Xtensa toolchain,
prints the entry point and the app's host-resolved dependency list, and packs
`build/hello_app.tar`. Drop the tar in `/apps/` on the device SD card and
install it through APPLOADER.

## Documentation

The SDK ships its own DocMD site (`docs/`): build it with `cd docs && npm
install && npm run build` and preview `docs/site/`. Published at
`https://talismandesign.github.io/PocketMage_SDK/docs`. Sources:

| Doc                                     | Covers                                                        |
| --------------------------------------- | ------------------------------------------------------------- |
| [App ABI](docs/docs/app-abi.md)         | The ELF binary contract and how the host runs it              |
| [Building](docs/docs/build.md)          | Toolchain, project layout, `make` targets                     |
| [Symbols](docs/docs/symbols.md)         | The symbol/export surface and the dependency contract         |
| [Publishing](docs/docs/publish.md)      | Packaging the delivery tar and what the installer expects     |
| [i18n](docs/docs/i18n.md)               | Translation catalogs, the generator, and the string ownership |
| [Migration](docs/docs/migration.md)     | Moving an existing app to the ELF flow                        |

## Layout

```text
PocketMage_SDK/
  tools/
    app.mk            # compile, strip, check, pack recipe
    symbols.py        # exported-symbol table generator
  symbols.list        # curated host-export surface the SDK commits to
  examples/
    hello_app/        # minimal runnable app (CI builds this)
  pocketmage_i18n/    # translation engine, base catalog, tools
  docs/               # DocMD docs site (config, pages, lockfile)
  .github/workflows/  # SDK CI (example build, i18n generation, docs deploy)
```

## CI

`.github/workflows/sdk.yml` installs the pinned espressif Xtensa
toolchain on a fresh runner, runs `make clean all check pack` on the example,
asserts the artifact's endianness and that the tar carries both members,
and exercises the i18n generator. `.github/workflows/docs.yml` builds the
DocMD site and deploys it to GitHub Pages. Keep both green.

## Contributing

See [CONTRIBUTING.md](CONTRIBUTING.md). The short version: never break the
binary contract, keep every exported symbol curated and accounted for, and
leave the example build green.