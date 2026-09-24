---
type: concept
title: "Publishing an app"
description: "The delivery tar, the 40x40 icon, asset sharing, and what APPLOADER does with them on the device."
source: "https://talismandesign.github.io/PocketMage_SDK/docs/publish/"
path: /publish/
updated: 2026-09-24
okf:
  generated_by: "@docmd/plugin-okf"
  generated_at: "2026-09-24T05:42:26.157Z"
---
---
title: "Publishing an app"
description: "The delivery tar, the 40x40 icon, asset sharing, and what APPLOADER does with them on the device."
---

# Publishing an app

The delivery unit is a plain `.tar` file placed in `/apps/` on the device SD
card and installed through APPLOADER on the device. Not gzipped, not a
directory of loose files: one tar.

`make pack` produces it. The tar's top level must hold the two members the
installer keys off:

| Member            | Required | Meaning                                 |
| ----------------- | -------- | --------------------------------------- |
| `<name>.app.elf`  | yes      | the app binary (`*.app.elf` suffix is detected) |
| `<name>_ICON.bin` | no       | 40x40 1-bit icon, 200 bytes             |

`<name>` is the base of the ELF filename and becomes the app's display name.
Keep it short and unique; it is also the `assets/` subdir name and the slot
subdirectory.

## Install behavior (APPLOADER)

1. Extract `/apps/<name>.tar` into `/apps/temp`, skipping macOS `._*` junk.
2. Find the first `*.app.elf`, derive `base` by stripping `.app.elf`.
3. Find `<base>_ICON.bin` anywhere in the extracted tree (case-insensitive).
   A missing icon is non-fatal; the default is shown instead.
4. If an `assets/` directory is present, copy it into `/assets/<base>`
   (flattened, best effort).
5. Install ELF + icon into the selected slot (`/apps/slot<n>`, 4 slots) and
   record metadata for the launcher and app switcher.

Slots are swapped wholesale: installing over a slot replaces its content and
clears stale legacy metadata for that slot. After a successful install, delete
the tar from `/apps` to free space on the card.

## Icon

`<name>_ICON.bin` is 200 bytes: a 40x40, 1-bit-per-pixel bitmap, 5 bytes per
row, 8 pixels per byte, MSB first. A set bit is black ink on the e-ink
display. An absent icon shows the OS default; build with one so the launcher
looks right.

## Assets

An optional top-level `assets/` directory in the tar is copied flat into
`/assets/<name>` at install. Apps read their files from there at runtime.
Nested directories and macOS metadata files inside `assets/` are tolerated
(metadata is skipped), but a flat layout is the reliable path.

## Before you ship

- Build from a clean checkout with the pinned toolchain:
  `make --no-print-directory -C <app> clean all check pack`.
- Inspect the artifact before shipping: `tar -tvf build/<name>.tar` and
  `readelf -h` on the ELF.
- Don't create the tar from your mac's Finder copies: `._*` files get skipped
  by the installer, but they bloat the package. `make pack` avoids it.
- Don't ship a `.bin` or the old OTA-style package. Production devices only
  install `/apps/*.tar`.
