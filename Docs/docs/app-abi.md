---
title: "App binary contract (ABI)"
description: "The binary shape the loader accepts, how it is mapped and run, and the failure modes that bite."
---

# App binary contract (ABI)

The loader accepts exactly one thing: a 32-bit `ET_DYN` ELF, little-endian, entry `app_main`. Anything else fails to load. Build and packaging mechanics live in [build.md](build.md); the symbol surface in [symbols.md](symbols.md); the overlay sidecar that ships beside the ELF in [overlay.md](overlay.md).

## Format

- `e_ident[EI_CLASS]` = `ELFCLASS32`
- `e_ident[EI_DATA]` = `ELFDATA2LSB`
- `e_type` = `ET_DYN` (position independent)
- entry = the `app_main` symbol

The loader reads the ELF structures with plain native reads. No byte swapping, no endianness validation. A big-endian file decodes into garbage and the load fails, which is why `app.mk` asserts endianness after link and refuses to keep a bad artifact.

Only the espressif `xtensa-esp-elf` toolchain emits output the loader can read. The PlatformIO package emits the wrong byte order and is unusable for apps; see [build.md](build.md) for install paths.

## Entry point

The host uses the ELF entry address, pinned to `app_main` by the linker, and runs it on core 1 in a dedicated task with a 16 KB stack. The stripped file need not retain the `app_main` symbol name:

```c
int main(int argc, char **argv);   // renamed to app_main at build time
```

The build passes `-Dmain=app_main -e app_main`, so an app writes its entry with C linkage:

```cpp
extern "C" int main(int argc, char **argv)
```

The runner passes four arguments, taken from the install record and the `app.properties` manifest:

| Index     | Value                                      |
| --------- | ------------------------------------------ |
| `argv[0]` | app name (manifest `name`, else base name) |
| `argv[1]` | absolute path of the loaded ELF            |
| `argv[2]` | app version string (manifest `version`)    |
| `argv[3]` | author string (manifest `author`)          |

Returning from `main` starts checked cleanup: audio stops, finalizers and app-local exit hooks run, and task-owned SD resources close while the code remains resident. Only then are the overlay and base image released and HOME reopened. See [App lifecycle](guides/lifecycle.md) for ordering, callback ownership, and retry behavior.

## How loading works

Loading is two stages: a base relocation with `esp_elf`, then an overlay pass with `elf_overlay`. The runner is `src/elf_runner.cpp`.

1. The scoped symbol resolver is installed first, because relocation is what asks the host to resolve each undefined symbol (see [symbols.md](symbols.md)). `esp_elf_init` then `esp_elf_relocate_source` relocate the base image. The ELF is streamed by offset from the SD file, so the whole file is never held in RAM; only transient section buffers add to the peak.
2. Relocated `.text` lands in `MALLOC_CAP_EXEC` memory so code is executable on the S3; data and rodata land in `MALLOC_CAP_8BIT` DRAM. Code in PSRAM is not executable on the ESP32-S3, app or firmware.
3. The `<name>.elfo` sidecar is located next to the ELF and loaded by `elf_overlay`, which pages overlay `.text` bodies into a pool of exec-RAM frames on demand, patches ISA-specific literal loads at page-in, applies redirects so resident calls route through stubs, and redirects the entry point through its stub. If no sidecar is present the app runs unredirected from the base relocation. The blob format and the full page-in sequence are in [overlay.md](overlay.md).

The pinned `esp_elf_request()` runs preinit/init arrays once before entry. The runner opens the app session before that call, so compiler-emitted destructor registrations belong to the app. `-fno-exceptions -fno-rtti -fno-threadsafe-statics` remain the app build defaults. Finalizer arrays run before deallocation, followed by any remaining session callbacks.

Apps link `-nostdlib`: runtime and SDK services resolve from the host. The small image-local exit-hook bridge is compiled into the app to keep C/C++ exit registrations out of the firmware's process-wide registry. Do not link a second newlib or libstdc++ into the app.

## Third-party libraries

An app can link any C or C++ library that compiles for Xtensa. The host exports the C and C++ runtime symbols, so a library's references to `malloc`, `memcpy`, `operator new`, and the soft-float helpers resolve against the host instead of needing a copy in the app.

Source is compiled into the app by overriding `APP_SRCS`:

```make
APP_SRCS = main.cpp third_party/cJSON.c
```

That keeps the app small. `tools/app.mk` links with `-Os`, `-ffunction-sections`, `--gc-sections` and `--strip-all`, so a library's unused functions are dropped: cJSON linked into an app comes to 5.2 KB rather than the size of its source.

Two limits worth knowing. The host exports runtime symbols the firmware already links, so a library needing a function nothing in the firmware uses will not resolve until the host exports it deliberately. And symbol counts depend on the app's own optimization level: the same `std::vector` source at `-O0` wants 13 host symbols, at `-Os` it wants 7. `app.mk` uses `-Os`.

`sscanf` is the one common libc function the host does not export. It exists in newlib but nothing in the firmware references it, so `--gc-sections` dropped it, and referencing it pulls in about 190 KB of stdio reentrancy. Prefer `strtod` or `strtol` in app code and in vendored libraries. See [symbols.md](symbols.md).

## Calling the SDK from an app

An app includes one header and calls plain C functions:

```cpp
#include <pm_app_api.h>

pm_i18n_set_language(PM_LANG_ENGLISH);
int w = pm_text_width(PM_TARGET_EINK, "Hello", PM_STYLE_BODY);
pm_oled_sysmsg("saved", 1500);
```

`pm_app_api.h` pulls in `pm_sdk_app.h`, which is generated from the SDK headers by `tools/gen_app_api.py`. Regenerate it after any SDK header change:

```sh
python3 tools/gen_app_api.py --root . --host-root ../..
python3 tools/gen_app_api.py --root . --host-root ../.. --verify
```

Names are `pm_<module>_<method>` with the method converted from camelCase: `Eink::refresh` becomes `pm_eink_refresh()`. Enums are plain integers in the app, named `PM_TARGET_*`, `PM_STYLE_*` and `PM_LANG_*`, and they are generated in the SDK's own declaration order, so a value always means the same language or target. `PM_APP_API_ABI` in the generated header changes when a signature does.

C++ types are projected at the boundary. `String` arguments become `const char*`, returned strings and `const char*` point into a small rotating pool that is overwritten by later calls, and an SDK method that needs the filesystem gets the host's global handle rather than taking one. Methods returning an SDK type an app cannot construct, such as `DateTime` or `WifiApInfo`, have no wrapper. Where the value matters, the accessor is flattened into plain fields instead: `pm_clock_epoch()` and `pm_clock_timestamp()` rather than a `DateTime` return.

Native SDK services own behavior and validation. Binary file I/O, directory enumeration, playback queries, image-region decoding, and OLED/e-ink canvas operations are generated wrappers over those services. SD access goes through the gateway task. The text `writeFile` path truncates on the first NUL and the string pool is 256 bytes; use the binary or stream APIs for larger data.

The full native stream contract is available as `pm_sd_stream_open_mode(path, mode)` (`'r'`, `'w'`, or `'a'`), `pm_sd_stream_read`, `pm_sd_stream_write`, `pm_sd_stream_seek_from(slot, offset, whence)` (`SEEK_SET`, `SEEK_CUR`, or `SEEK_END` from `<stdio.h>`), and `pm_sd_stream_close_checked(slot)`. Close reports flush/close errors. The slot is released even if close returns false. `pm_sd_stream_tell` and `pm_sd_stream_size` return zero on error.

New apps should use those generated functions. ABI 2's read-only `pm_sd_stream_open`, absolute `pm_sd_stream_seek`, and void `pm_sd_stream_close` remain compatibility adapters. The absolute adapter rejects offsets larger than `LONG_MAX`. `pm_image_decode` takes an explicit fit/fill choice; the older `pm_image_decode_to_1bit` retains fit-only behavior.

`pm_app_api.h` owns the remaining C adapters: checked font-enum conversion, legacy ink-color semantics, jingle indices, flattened clock/image values, activity reporting, and compatibility signatures. `pm_overlay_api.h` owns the OS overlay-provider declarations. The generator reads their declarations directly into both export manifests; there is no separate handwritten symbol registry. `ADOPTED` validates projected adapter signatures and `GENERATED_NAMES` selects stable C names for ordinary wrappers. Conflicting owners and ambiguous overloads fail generation.

A function returning `std::vector<String>`, such as word wrapping, becomes a count call and an indexed getter, matching how the SDK already hands out scan results:

```cpp
char line[80];
int lines = pm_layout_word_wrap_count(text, maxWidth, PM_STYLE_BODY);
for (int i = 0; i < lines; i++) {
  pm_layout_word_wrap_get(text, maxWidth, PM_STYLE_BODY, i, line, sizeof line);
  draw(line);
}
```

The getter writes into a buffer the caller owns rather than the string pool, because a wrapped paragraph can be far longer than the pool holds, and it returns `-1` for an out of range index. A function taking `const std::vector<String>&` takes a C array and a count instead: `pm_io_join_string(const char* const* items, int count, char delimiter)`.

Regeneration updates `pm_sdk_app.h`, the OS wrapper source, `symbols.list`, and `host_exports.list`. `--verify` checks all four without writing and fails on stale signatures or exports. `--check` prints the projected and skipped surface. After changing exports, regenerate the firmware's `esp_all_symbol.cpp` with `tools/symbols.py` as described in [symbols.md](symbols.md), then rebuild the OS. A symbol must appear in the loader's compiled export table to resolve at runtime; a firmware global alone is insufficient.

## SDK version stamp

The host exports the ABI version as a string symbol, defined in `pocketmage_globals.cpp` and curated in `symbols.list`:

```cpp
extern "C" const char pocketmage_sdk_version[];  // e.g. "0.1.0"
```

Apps consume it through the macro surface in `pocketmage_app_version.h`: `POCKETMAGE_SDK_VERSION_STRING`, `POCKETMAGE_SDK_VERSION_MAJOR`, `POCKETMAGE_SDK_VERSION_MINOR`, `POCKETMAGE_SDK_VERSION_PATCH`. The firmware loads these macros from `-DPM_SDK_VERSION_*` supplied by `app.mk`, and the OS's loader export table (generated from `symbols.list` via `tools/symbols.py`) must contain the matching string symbol.

Keeping VERSION, the string literal, the header macros, and `library.json` in sync is enforced by the CI `version-sync` job and `pm release`, which rewrites all four together.

## Runtime environment

- The app shares the host's heap. Loading costs roughly the ELF size plus a fixed reserve; `runElfApp` refuses to launch when free `MALLOC_CAP_8BIT` drops below that floor. Keep the app small.
- The app runs in-process. It can call anything the host exports - the display, keyboard stack, translation engine, peripheral drivers - at native speed, no IPC.
- Stack is not heap. The run task gives 16 KB; deep recursion or fat frames blow it. Move working sets to the heap.
- E-ink refreshes use the differential partial waveform while an app runs, regardless of the device's FAST_REFRESH setting; the setting is restored when the app exits. Redraw on change, not on a timer: even partial updates are visible, and a periodic slow clean still flashes the panel.

## Sections and strip

`app.mk` links with `--gc-sections` (functions and data) and strips loader-irrelevant sections, so the delivered ELF comes down to `.text`, `.rodata`, `.got`, `.rela.dyn`, `.rela.plt`, `.dynsym`, `.dynstr`, `.hash`, `.shstrtab`. Everything else is removed. Code that depends on other sections existing is unsupported. Don't write it.

The overlay sidecar is derived from the unstripped link output, not from this delivered ELF, so stripping the ELF does not lose the data the overlay needs. See [overlay.md](overlay.md).

## App manifest (`app.properties`)

When loading an app from SD, the host reads `app.properties` from the app directory (max 1024 bytes). Recognized fields:

- `name` - human-readable app name
- `version` - app version string
- `author` - author string, free form; multiple authors may be comma separated (spaces are kept, up to 31 bytes)
- `scope` - symbol scope gate (default: `all`)

Only `pm_*` symbols are scope-gated. libc, libstdc++, and ESP-IDF symbols remain available regardless of scope. The loader installs the scoped resolver before relocation and resets it on exit. argv metadata passed to `app_main` includes `name`, `elfPath`, `version`, and `author`.

## Endianness failure modes

| Symptom at load                              | Cause                               |
| -------------------------------------------- | ----------------------------------- |
| garbage / instruction fetch fault in IRAM    | ELF fields decoded wrong (BE)       |
| `esp_elf_load_section` fault                 | misaligned / incorrect section data |
| load "succeeds", entry behaves wildly        | wrong sizes from BE headers         |

The build-time guard catches BE before it reaches a device: if the last `make` line is `error: ... big-endian ...`, rebuild with the espressif toolchain (see [build.md](build.md)).

## Stability rules for SDK authors

- The format, entry point, and load strategy are public contract. Changing how an app maps or invokes, including the ELFO overlay format in [overlay.md](overlay.md), takes a `major` bump of `pocketmage_sdk_version` and a migration note.
- Exports are additive. Removing or changing an export breaks live apps. See [CONTRIBUTING.md](https://github.com/TalismanDesign/PocketMage_SDK/blob/main/CONTRIBUTING.md).
