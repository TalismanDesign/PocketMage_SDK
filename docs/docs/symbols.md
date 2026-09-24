---
title: "Symbols and the dependency contract"
description: "The host-export surface apps resolve against, how to read an app's dependency list, and the rules for changing the surface."
---

# Symbols and the dependency contract

Apps link `-nostdlib`. Nothing ships with the binary that satisfies a
reference; at load time the host resolves every undefined symbol from its own
live memory. The app's undefined-symbol list is therefore its real dependency
contract. `make check` prints it; `pm check` interprets it against the host
surface.

## The surface

An app can call anything the host exports as a global symbol: the
newlib/IDF/Arduino runtime plus the curated PocketMage export table. The
curated table is generated from `host_exports.list` (in PocketMageOS) by
`tools/symbols.py`, which builds the `g_customer_elfsyms` table the loader
searches.

The SDK's `symbols.list` is the promise of what the host exports. Today:

```text
OLED
KB
EINK
BZ
CLOCK
PM_SDAUTO
pocketmage_sdk_version
```

Host-side, `src/ELF_SYMS/host_exports.list` tracks a superset-or-equal,
validated against the firmware image; a missing host export degrades to a
loud warning before the generated table is emitted.

Regular libc calls (`printf`, `puts`, `sleep`, `malloc`, ...) resolve from the
host's newlib globals and need no entry in the curated lists.

## Reading an app's contract

```sh
make check   # prints entry + every undefined symbol
pm check     # classes each symbol and exits non-zero on a broken contract
```

Everything in that list must exist in the host's global view when the app
loads, or the load fails. `pm check` is the exact gate when run
against a host firmware ELF (`--host-elf`): every undefined symbol must resolve
to a host global. Without a host ELF it falls back to the curated lists plus a
libc allowlist and reports anything outside that set as unconfirmed.

## Editing the surface

Rules for SDK authors:

- Additions only. Removing or renaming an exported symbol breaks every app
  that references it at runtime.
- Every added symbol must exist as a `GLOBAL` in the firmware image and be
  added to both `symbols.list` and the host's `host_exports.list`, then
  regenerate the host table with `tools/symbols.py --host-elf ...`.
- Run `pm exports` (add `--all` for extras too) before committing a surface
  change: it reconciles `symbols.list` against the host's `host_exports.list`
  and fails when a curated symbol is not exported. Keep both lists in sync
  in the same change.
- Bump `pocketmage_sdk_version` when the surface changes. It is exported, and
  stored so the OS can version apps against the surface.
- Keep the set small. Exporting a broad slice of the firmware binds the ABI
  to all of it.

## Versioning and gating

`pocketmage_sdk_version` is exported so apps can adapt to surface changes.
`pm check` gates an app against the host export set before release: the mirror
of `make check`, with the curated lists as the reference when no host ELF is
available.

Ship a new version with `pm release <major|minor|patch>` (add
`--message TEXT` for a CHANGELOG bullet). It validates VERSION, the
`pocketmage_sdk_version` literal in `pocketmage_globals.cpp`,
`pocketmage_app_version.h`, and `library.json` first, then rewrites all four
and folds an `[N.N.N]` header at the top of `CHANGELOG.md`. No write happens
if any file disagrees. The command stages nothing in git; commit the four
files and the changelog together.