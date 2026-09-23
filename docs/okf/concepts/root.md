---
type: concept
title: "PocketMage SDK Documentation"
description: "Official SDK for building PocketMage apps: the ELF app contract, build tooling, symbol surface, publishing flow, and the migration path."
source: "https://talismandesign.github.io/PocketMage_SDK/docs/"
path: /
updated: 2026-09-23
okf:
  generated_by: "@docmd/plugin-okf"
  generated_at: "2026-09-23T04:49:51.780Z"
---
---
title: "PocketMage SDK Documentation"
description: "Official SDK for building PocketMage apps: the ELF app contract, build tooling, symbol surface, publishing flow, and the migration path."
---

# PocketMage SDK Documentation

PocketMage apps are ELF files the OS loads at runtime and runs in-process.
These docs cover the binary contract, the build tooling, the symbol surface,
packaging, i18n, and the migration path off the old firmware-flow apps.

::: tag "App ABI"
::: tag "Build tooling"
::: tag "Symbols"
::: tag "Publishing"
::: tag "Migration"

## Sections

::: grids
::: grid
::: card "App ABI" icon:book
The binary shape the loader accepts: format, entry, load strategy, failure
modes.

::: button "Open" ./app-abi.md icon:arrow-right
:::
:::
::: grid
::: card "Building Apps" icon:code
A `Makefile` including `tools/app.mk` is the whole build system. Toolchain,
targets, icon.

::: button "Open" ./build.md icon:arrow-right
:::
:::
::: grid
::: card "Symbols" icon:box
The host-export surface an app's undefined list resolves against, and how to
change it.

::: button "Open" ./symbols.md icon:arrow-right
:::
:::
::: grid
::: card "Publishing" icon:package
The delivery tar, the 40x40 icon, assets, and what APPLOADER does with them.

::: button "Open" ./publish.md icon:arrow-right
:::
:::
::: grid
::: card "i18n" icon:terminal
Catalogs, the generator, and the merge rules that keep string IDs stable.

::: button "Open" ./i18n.md icon:arrow-right
:::
:::
::: grid
::: card "Migration" icon:book-open
Moving a pre-ELF app to the runtime-load flow.

::: button "Open" ./migration.md icon:arrow-right
:::
:::
:::

## Start here

::: steps

1. Read the [App ABI](app-abi.md) contract.
2. Set up the [build toolchain](build.md).
3. Check what your app resolves against: [Symbols](symbols.md).
4. Ship a tar: [Publishing](publish.md).
5. Port existing code: [Migration](migration.md).

:::

## Links

- [Reference example app](https://github.com/TalismanDesign/PocketMage_SDK/tree/main/examples/hello_app)
- [PocketMageOS documentation](https://talismandesign.github.io/PocketMage_PDA/docs)
- [GitHub Repository](https://github.com/TalismanDesign/PocketMage_SDK)
- [Discord Community](https://discord.gg/KSCapSf4XH)
