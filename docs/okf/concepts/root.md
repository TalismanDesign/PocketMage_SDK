---
type: concept
title: "PocketMage SDK Documentation"
description: "Official SDK for building PocketMage apps: get started, the full component API reference, the ELF app contract, build tooling, and publishing."
source: "https://talismandesign.github.io/PocketMage_SDK/docs/"
path: /
updated: 2026-10-31
okf:
  generated_by: "@docmd/plugin-okf"
  generated_at: "2026-10-31T22:45:59.837Z"
---
---
title: "PocketMage SDK Documentation"
description: "Official SDK for building PocketMage apps: get started, the full component API reference, the ELF app contract, build tooling, and publishing."
---

# PocketMage SDK Documentation

A PocketMage app is a standalone ELF the host firmware loads at runtime and runs in-process. There is no firmware reflash: you build an ELF, pack it with an overlay sidecar and an icon, copy the tar onto the SD card, and install it through the on-device app loader. These docs cover the whole path, from installing the toolchain to the binary contract the loader enforces.

::: tag "Get Started"
::: tag "Guides"
::: tag "API reference"
::: tag "Binary contract"
::: tag "Tooling"
::: tag "Publishing"

## Get started

New here? Go from an empty directory to a running app in two steps.

::: grids
::: grid
::: card "Get Started" icon:rocket
What a PocketMage app is, what you need installed, and the shortest path to a running ELF.

::: button "Open" ./get-started/index.md icon:arrow-right
:::
:::
::: grid
::: card "Install the toolchain" icon:wrench
The pinned espressif Xtensa compiler, the `elf_overlay` host tools, and the `pm` CLI.

::: button "Open" ./get-started/toolchain.md icon:arrow-right
:::
:::
::: grid
::: card "Your first app" icon:play
Scaffold, build, gate, pack, and install a minimal app end to end.

::: button "Open" ./get-started/first-app.md icon:arrow-right
:::
:::
:::

## Guides

Walkthroughs of the whole app model.

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
::: card "Internationalization" icon:globe
Give an app translatable strings: the catalog it ships and the runtime that resolves them.

::: button "Open" ./guides/i18n.md icon:arrow-right
:::
:::
::: grid
::: card "Publishing" icon:package
The delivery tar, the 40x40 icon, assets, and what APPLOADER does with them.

::: button "Open" ./guides/publishing.md icon:arrow-right
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

Every component the SDK publishes, with semantics and its `pm_*` C ABI. Start at the overview.

::: grids
::: grid
::: card "API overview" icon:boxes
The umbrella header, the `PM_TARGET_HOST`/`PM_TARGET_APP` split, the singleton pattern, and the `pm_*` projection model.

::: button "Open" ./api/index.md icon:arrow-right
:::
:::
::: grid
::: card "Function index" icon:list
Every `pm_*` wrapper and its exact signature, grouped by module.

::: button "Open" ./api/functions.md icon:arrow-right
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

What the loader accepts and how an app's dependencies resolve.

::: grids
::: grid
::: card "App ABI" icon:book
The binary shape the loader accepts: format, entry, two-stage load, failure modes.

::: button "Open" ./app-abi.md icon:arrow-right
:::
:::
::: grid
::: card "Overlay format (.elfo)" icon:layers
The overlay sidecar that pages app code into executable RAM, and what it contains.

::: button "Open" ./overlay.md icon:arrow-right
:::
:::
::: grid
::: card "Symbols" icon:box
The host-export surface an app's undefined list resolves against, and how to change it.

::: button "Open" ./symbols.md icon:arrow-right
:::
:::
:::

## Tooling

The build and release tools.

::: grids
::: grid
::: card "Building apps" icon:wrench
A `Makefile` including `tools/app.mk` is the whole build system. Toolchain, targets, icon, overlay.

::: button "Open" ./build.md icon:arrow-right
:::
:::
::: grid
::: card "pm CLI" icon:terminal
Scaffolding, building, the enforcing dependency gate, packing, exports, and release bumps.

::: button "Open" ./cli.md icon:arrow-right
:::
:::
::: grid
::: card "i18n catalogs" icon:globe
The generator and the merge rules that keep string IDs stable across the SDK and app catalogs.

::: button "Open" ./i18n.md icon:arrow-right
:::
:::
:::

## Help

::: grids
::: grid
::: card "Troubleshooting" icon:alert-triangle
The failures a build or load hits most, what causes each, and how to fix it.

::: button "Open" ./troubleshooting.md icon:arrow-right
:::
:::
::: grid
::: card "Glossary" icon:book-open
Every term these docs use, defined once, with links to the page that owns the concept.

::: button "Open" ./glossary.md icon:arrow-right
:::
:::
::: grid
::: card "Migration" icon:git-branch
Moving a pre-ELF app to the runtime-load flow.

::: button "Open" ./migration.md icon:arrow-right
:::
:::
:::

## Start here

::: steps

1. Install the toolchain: [Get Started](get-started/toolchain.md).
2. Build your first app end to end: [Your first app](get-started/first-app.md).
3. Learn the model: [Making an app](guides/making-apps.md), then the [API overview](api/index.md).
4. Read the contract: [App ABI](app-abi.md) and [Overlay format](overlay.md).
5. Check what your app resolves against: [Symbols](symbols.md).
6. Ship a tar: [Publishing](guides/publishing.md).
7. Port existing code: [Migration](migration.md).

:::

## Links

- [App template repository](https://github.com/TalismanDesign/PocketMage_App)
- [PocketMageOS documentation](https://talismandesign.github.io/PocketMage_PDA/docs)
- [GitHub Repository](https://github.com/TalismanDesign/PocketMage_SDK)
