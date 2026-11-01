---
type: guide
title: "PocketMage SDK guides"
description: "How to build apps, understand their lifecycle, and draw to both displays."
source: "https://talismandesign.github.io/PocketMage_SDK/docs/guides/"
path: /guides/
updated: 2026-11-01
okf:
  generated_by: "@docmd/plugin-okf"
  generated_at: "2026-11-01T01:09:38.812Z"
---
---
title: "PocketMage SDK guides"
description: "How to build apps, understand their lifecycle, and draw to both displays."
---

# SDK Guides

These guides teach the PocketMage application model end to end. They pair with the [app ABI reference](../app-abi.md) and the [API reference](../api/).

## [Making an app](making-apps.md)

From `pm new` to a running ELF: the app template, building with the SDK's make recipe, the delivery tar, and how the host loads it.

## [App lifecycle](lifecycle.md)

Entry point conventions, the `APP_INIT` hook, the bidirectional host/app export model, and how exiting returns control to the OS.

## [Rendering](rendering.md)

The two-display model: which draw calls target which panel, the E-Ink buffered refresh model, and the touch/scroll interaction loop.

## [Internationalization](i18n.md)

Give an app translatable strings: the catalog an app ships, how it merges with the SDK base catalog, and the runtime engine that resolves them.

## [Publishing an app](publishing.md)

The delivery tar, the 40x40 icon, asset sharing, and what APPLOADER does with them on the device.

## Also see

- [App ABI and binary layout](../app-abi.md)
- [Overlay format (`.elfo`)](../overlay.md)
- [Building apps](../build.md)
- [Export table and `symbols.list`](../symbols.md)
- [i18n catalog tooling](../i18n.md)
