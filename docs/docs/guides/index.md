---
title: "PocketMage SDK guides"
description: "How to build apps, understand their lifecycle, and draw to both displays."
---

# SDK Guides

These guides teach the PocketMage application model end to end. They pair with the [app ABI reference](app-abi.md) and the [API reference](../api/).

## [Making an app](making-apps.md)

From `pm new` to a running ELF: the app template, building with the SDK's make recipe, the delivery tar, and how the host loads it.

## [App lifecycle](lifecycle.md)

Entry point conventions, the `APP_INIT` hook, the bidirectional host/app export model, and how exiting returns control to the OS.

## [Rendering](rendering.md)

The two-display model: which draw calls target which panel, the E-Ink buffered refresh model, and the touch/scroll interaction loop.

## Also see

- [App ABI and binary layout](app-abi.md)
- [Build pipeline for the host firmware](build.md)
- [Export table and `symbols.list`](../symbols.md)
- [Publishing an app](../publish.md)
- [Internationalization catalogs](../i18n.md)
