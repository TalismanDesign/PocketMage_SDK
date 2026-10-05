---
title: "PocketMage SDK Documentation"
description: "Official SDK for building PocketMage apps: guides, the full component API reference, the ELF app contract, and publishing."
---

# PocketMage SDK Documentation

PocketMage apps are ELF files the host firmware loads at runtime and runs in-process. These docs cover how to write an app, every component of the app API, the binary contract, the build tooling, the symbol surface, and packaging.

::: tag "Guides"
::: tag "API reference"
::: tag "App ABI"
::: tag "Build tooling"
::: tag "Symbols"
::: tag "Publishing"

## Guides

Start here. Walkthroughs of the whole app model.

::: grids
::: grid
::: card "Making an app" icon:code
From `pm new` to a running ELF: scaffold, build recipe, delivery tar, and the host loader.

::: button "Open" ./guides/making-apps.md icon:arrow-right
:::
:::
::: grid
::: card "App lifecycle" icon:activity
Entry points, the host/app export model, `APP_INIT`, and exiting cleanly.

::: button "Open" ./guides/lifecycle.md icon:arrow-right
:::
:::
::: grid
::: card "Rendering" icon:monitor
The two-display model, the buffered E-Ink refresh rhythm, and the scroll loop.

::: button "Open" ./guides/rendering.md icon:arrow-right
:::
:::
::: grid
::: card "Guides overview" icon:book
Guide index and cross-links into the deep references.

::: button "Open" ./guides/index.md icon:arrow-right
:::
:::
:::

## API reference

Every header the SDK publishes, with semantics. Start at the overview.

::: grids
::: grid
::: card "API overview" icon:boxes
The umbrella header, the `PM_TARGET_HOST`/`PM_TARGET_APP` split, and how to read the per-component pages.

::: button "Open" ./api/index.md icon:arrow-right
:::
:::
::: grid
::: card "Displays" icon:monitor
OLED, E-Ink, Font, Layout, UI helpers.

::: button "Open" ./api/oled.md icon:arrow-right
:::
:::
::: grid
::: card "Input" icon:keyboard
Keyboard, Touch.

::: button "Open" ./api/kb.md icon:arrow-right
:::
:::
::: grid
::: card "System" icon:cpu
Sys and globals, SD card, Clock, Buzzer, WiFi, i18n runtime.

::: button "Open" ./api/sys.md icon:arrow-right
:::
:::
:::

## The binary contract

How the framework itself is built and shipped.

::: grids
::: grid
::: card "App ABI" icon:book
The binary shape the loader accepts: format, entry, load strategy, failure modes.

::: button "Open" ./app-abi.md icon:arrow-right
:::
:::
::: grid
::: card "Building Apps" icon:wrench
A `Makefile` including `tools/app.mk` is the whole build system. Toolchain, targets, icon.

::: button "Open" ./build.md icon:arrow-right
:::
:::
::: grid
::: card "Symbols" icon:box
The host-export surface an app's undefined list resolves against, and how to change it.

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
::: card "i18n catalogs" icon:terminal
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

1. Read the [guides](guides/making-apps.md), then the [API overview](api/index.md).
2. Read the [App ABI](app-abi.md) contract.
3. Set up the [build toolchain](build.md).
4. Check what your app resolves against: [Symbols](symbols.md).
5. Ship a tar: [Publishing](publish.md).
6. Port existing code: [Migration](migration.md).

:::

## Links

- [App template repository](https://github.com/TalismanDesign/PocketMage_App)
- [PocketMageOS documentation](https://talismandesign.github.io/PocketMage_PDA/docs)
- [GitHub Repository](https://github.com/TalismanDesign/PocketMage_SDK)
