---
title: "Get Started"
description: "What a PocketMage app is, what you need installed, and the shortest path from an empty directory to a running ELF."
---

# Get Started

A PocketMage app is a standalone ELF the device loads and runs at runtime. There is no firmware reflash: you build an ELF, pack it with an overlay sidecar and an icon, copy the tar onto the SD card, and install it through the on-device app loader. The host resolves everything the app references, from `malloc` to the `pm_*` API, at load time.

This section takes you from nothing to a running app in two steps.

::: steps

1. [Install the toolchain](toolchain.md) - the pinned espressif Xtensa compiler, the `elf_overlay` host tools, and the `pm` CLI.
2. [Build your first app](first-app.md) - scaffold, build, gate, pack, and install.

:::

## How the pieces fit

| Piece              | Role                                                              |
| ------------------ | ----------------------------------------------------------------- |
| SDK repo           | the app-facing contract: headers, the `pm_*` ABI, the build recipe, the base i18n catalog |
| Xtensa toolchain   | compiles and links the app into a 32-bit `ET_DYN` ELF             |
| `elf_overlay` tools| turn the link output into the `.elfo` overlay sidecar            |
| `pm` CLI           | scaffolds, builds, gates, and packs (wraps `make`)                |
| App template       | the starting point `pm new` clones from                           |

## Where to go next

- The whole app model, end to end: [Making an app](../guides/making-apps.md).
- The binary contract the loader enforces: [App ABI](../app-abi.md).
- Every function an app can call: [Function index](../api/functions.md).
- The device side of loading, installing, and slots: [PocketMageOS docs](https://talismandesign.github.io/PocketMage_PDA/docs).
