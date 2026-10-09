---
type: concept
title: "Symbols and the dependency contract"
description: "The host-export surface apps resolve against, how third-party libraries link, and the rules for changing the surface."
source: "https://talismandesign.github.io/PocketMage_SDK/docs/symbols/"
path: /symbols/
updated: 2026-10-09
okf:
  generated_by: "@docmd/plugin-okf"
  generated_at: "2026-10-09T11:27:10.781Z"
---
---
title: "Symbols and the dependency contract"
description: "The host-export surface apps resolve against, how third-party libraries link, and the rules for changing the surface."
---

# Symbols and the dependency contract

Apps link `-nostdlib`. Nothing ships with the binary that satisfies a
reference; at load time the host resolves every undefined symbol from its own
live memory. The app's undefined-symbol list is therefore its real dependency
contract. `make check` prints it; `pm check` interprets it against the host
surface.

## The surface

An app can call anything the host exports as a global symbol. The export
surface has two parts.

**The C and C++ runtime.** `libc`, `libm`, `libgcc`, `libstdc++` and
`libsupc++` symbols the firmware already links are exported so apps can use
third-party libraries. `tools/symbols.py` derives this set by reading the
toolchain archives with `nm` and keeping the symbols that are present in the
firmware image:

```sh
python3 tools/symbols.py --list symbols.list --host-elf <firmware.elf> \
  --print-cost
```

```
libc        250 symbols   4,467 bytes
libm         15 symbols     262 bytes
libstdc++    87 symbols   3,403 bytes
libsupc++    58 symbols   2,362 bytes
libgcc       65 symbols   1,552 bytes
libnewlib    94 symbols   2,478 bytes
total       602 symbols  15,009 bytes (14.7 KB)
```

That total includes the curated `pm_*` entries. The runtime adds about 11.2 KB
of flash in the firmware image, once. It is not a per-app cost. Apps stay
small: a hello world is 1.3 KB, an app using `std::vector` and `std::string`
is 2.3 KB, an app with cJSON linked in is 5.2 KB. `--gc-sections` and
`--strip-all` in `tools/app.mk` do that work.

A symbol in `symbols.list` that the firmware does not define is reported as a
warning but still emitted into the table. A newly added wrapper cannot appear
in `--host-elf` until the host is rebuilt, so dropping it would omit it on the
one run that adds it. Regenerate once more after building and the warning
should be gone.

Runtime export is on by default. Pass `--no-runtime` to export only the
curated SDK list, which restricts apps to the SDK surface and no libraries.

Membership is decided by which archive a symbol comes from, not by its name.
A `_ZN...` prefix also matches the SDK's own C++ classes, so matching on prefix
shape would export firmware internals by accident.

GCC reserves `__atomic_*`, `__sync_*` and `__builtin_*` names as builtins and
rejects taking their address. Those exports are declared under a private
`pm_elfsym_` identifier with an asm label, then listed by their literal name in
the table.

**The curated SDK table.** `symbols.list` is the hand-maintained promise of
PocketMage's own exports. Host-side, `src/ELF_SYMS/host_exports.list` tracks a
superset, validated against the firmware image. Both lists feed the same
generated `g_customer_elfsyms` table.

## Linking a third-party library

A vendored library's source is compiled into the app by overriding `APP_SRCS`.
Anything the library references that is not in the app is resolved by the host
at load time. Two consequences are worth knowing.

Libraries compiled at `-Os` need fewer symbols than the same source at `-O0`.
A `std::vector<std::string>` app at `-O0` wants 13 symbols including
`std::allocator<char>` constructors; at `-Os` it wants 7, and all 7 resolve.

`sscanf` is not exported by default. It exists in newlib, but nothing in the
firmware references it, so it was dropped by `--gc-sections`, and referencing
it pulls in about 190 KB of stdio reentrancy. A library that needs it must
either avoid it or the host must export it deliberately.

## Reading an app's contract

```sh
make check   # prints entry + every undefined symbol
pm check     # classes each symbol and exits non-zero on a broken contract
```

Everything in that list must exist in the host's global view when the app
loads, or the load fails. `pm check` with `--host-elf` is the exact gate: every
undefined symbol must resolve to a host global.

Without a host ELF, `pm check` reads the generated export table that the
firmware links, which is the same file the loader searches. It does not keep a
separate allowlist, because a hand-written list drifts from the table and the
drift shows up only on device. An earlier allowlist claimed `memmove` and
`sprintf` were resolvable when no table exported them, so those apps passed the
gate and failed at load with `Can't find common memmove`. Reading the generated
table makes that class of bug impossible.

## Editing the surface

Rules for SDK authors:

- Additions only. Removing or renaming an exported symbol breaks every app
  that references it at runtime.
- Every added SDK symbol must exist as a `GLOBAL` in the firmware image and be
  added to both `symbols.list` and the host's `host_exports.list`, then
  regenerate the host table with `tools/symbols.py --host-elf ...`.
- Regenerate the host table whenever the firmware's linked symbol set changes.
  `symbols.py` only exports runtime symbols the firmware already links, so a
  firmware that starts using a new libc or libstdc++ function makes it
  available on the next regeneration.
- Run `pm exports` before committing a surface change. It reconciles
  `symbols.list` against `host_exports.list` and fails when a curated symbol is
  not exported.
- Bump `pocketmage_sdk_version` when the surface changes. It is exported, and
  stored so the OS can version apps against the surface.

Exporting a broad slice of the firmware binds the ABI to all of it. That is a
deliberate trade here: it is what lets apps use libraries, and per-app
restriction is planned as an opt-in scope rather than a default.

## Versioning and gating

`pocketmage_sdk_version` is exported so apps can adapt to surface changes.
`pm check` gates an app against the host export set before release: the mirror
of `make check`, with the generated table as the reference when no host ELF is
available.

Ship a new version with `pm release <major|minor|patch>` (add
`--message TEXT` for a CHANGELOG bullet). It validates VERSION, the
`pocketmage_sdk_version` literal in `pocketmage_globals.cpp`,
`pocketmage_app_version.h`, and `library.json` first, then rewrites all four
and folds an `[N.N.N]` header at the top of `CHANGELOG.md`. No write happens
if any file disagrees. The command stages nothing in git; commit the four
files and the changelog together.
