---
title: "App binary contract (ABI)"
description: "The binary shape the loader accepts, how it is mapped and run, and the failure modes that bite."
---

# App binary contract (ABI)

The loader accepts exactly one thing: a 32-bit `ET_DYN` ELF, little-endian,
entry `app_main`. Anything else fails to load. Build and packaging mechanics
live in [build.md](build.md); the symbol surface in [symbols.md](symbols.md).

## Format

- `e_ident[EI_CLASS]` = `ELFCLASS32`
- `e_ident[EI_DATA]` = `ELFDATA2LSB`
- `e_type` = `ET_DYN` (position independent)
- entry = the `app_main` symbol

The loader reads the ELF structures with plain native reads. No byte
swapping, no endianness validation. A big-endian file decodes into garbage
and the load fails, which is why `app.mk` asserts endianness after link and
refuses to keep a bad artifact.

Only the espressif `xtensa-esp-elf` toolchain emits output the loader can
read. The PlatformIO package emits the wrong byte order and is unusable for
apps; see [build.md](build.md) for install paths.

## Entry point

The host looks up `app_main` by name and calls it on core 1 in a task with a
16 KB stack:

```c
int main(int argc, char **argv);   // renamed to app_main at build time
```

The build passes `-Dmain=app_main -e app_main`, so an app writes its entry
with C linkage:

```cpp
extern "C" int main(int argc, char **argv)
```

`argv[0]` is the app base name, `argv[1]` is the absolute path of the loaded
ELF. Returning from `main` tears the app down (the load is deinitialized) and
control returns to the OS. There is no run-forever semantics; a UI app hands
control back through the SDK/host API cooperatively, then returns. End the
lifetime. A `main` that never returns leaks the run task.

## How loading works

The host vendors the upstream `esp_elf_loader` (see `lib/ELFLoader`). At load
time:

1. Text maps into `MALLOC_CAP_EXEC` memory (IRAM) through the bus address
   mirror, so code is executable on the S3. Do not enable
   `CONFIG_ELF_LOADER_LOAD_PSRAM`: on ESP32-S3, code in PSRAM is not
   executable, app or firmware.
2. Data/rodata maps into `MALLOC_CAP_8BIT` DRAM segments.
3. Every relocation and every undefined symbol resolves from the host's own
   live memory.

The upstream `esp_elf` loader does not run relocation-time constructors, so
any `.init_array` the app links in is never executed. Keep the app free of
constructor sections (`-fno-exceptions -fno-rtti -fno-threadsafe-statics` are
on by default in `app.mk`); `pm check` warns if a `.init_array` slips in.
Do not rely on static-construction order at load time.

So apps link `-nostdlib`: no newlib, no libstdc++, no SDK copy in the ELF.
Everything an app references must resolve at load time from the host. Linking
newlib into the app is wasteful and collides with host globals. Don't.

## Third-party libraries

An app can link any C or C++ library that compiles for Xtensa. The host
exports the C and C++ runtime symbols, so a library's references to `malloc`,
`memcpy`, `operator new`, and the soft-float helpers resolve against the host
instead of needing a copy in the app.

Source is compiled into the app by overriding `APP_SRCS`:

```make
APP_SRCS = main.cpp third_party/cJSON.c
```

That keeps the app small. `tools/app.mk` links with `-Os`,
`-ffunction-sections`, `--gc-sections` and `--strip-all`, so a library's
unused functions are dropped: cJSON linked into an app comes to 5.2 KB rather
than the size of its source.

Two limits worth knowing. The host exports runtime symbols the firmware
already links, so a library needing a function nothing in the firmware uses
will not resolve until the host exports it deliberately. And symbol counts
depend on the app's own optimization level: the same `std::vector` source at
`-O0` wants 13 host symbols, at `-Os` it wants 7. `app.mk` uses `-Os`.

`sscanf` is the one common libc function the host does not export. It exists
in newlib but nothing in the firmware references it, so `--gc-sections` dropped
it, and referencing it pulls in about 190 KB of stdio reentrancy. Prefer
`strtod` or `strtol` in app code and in vendored libraries. See
[symbols.md](symbols.md).

## Calling the SDK from an app

An app includes one header and calls plain C functions:

```cpp
#include <pm_app_api.h>

pm_i18n_set_language(PM_LANG_ENGLISH);
int w = pm_text_width(2, "Hello", 2);
pm_oled_sysmsg("saved", 1500);
```

`pm_app_api.h` pulls in `pm_sdk_app.h`, which is generated from the SDK
headers by `tools/gen_app_api.py`. Regenerate it after any SDK header change:

```sh
python3 tools/gen_app_api.py
```

Names are `pm_<module>_<method>` with the method converted from camelCase:
`Eink::refresh` becomes `pm_eink_refresh()`. Enums are plain integers in the app,
named `PM_TARGET_*`, `PM_STYLE_*` and `PM_LANG_*`, and they are generated in the
SDK's own declaration order, so a value always means the same language or
target. `PM_APP_API_ABI` in the generated header changes when a signature does.

C++ types are projected at the boundary. `String` arguments become
`const char*`, returned strings and `const char*` point into a small rotating
pool that is overwritten by later calls, and an SDK method that needs the
filesystem gets the host's global handle rather than taking one. Methods
returning an SDK type an app cannot construct, such as `DateTime` or
`WifiApInfo`, have no wrapper. Where the value matters, the accessor is
flattened into plain fields instead: `pm_clock_epoch()` and
`pm_clock_timestamp()` rather than a `DateTime` return.

Blob storage and directory creation have no SDK method to project. The text
`writeFile` path truncates on the first NUL and the read pool is 256 bytes, so
`pm_sd_write_binary_file()` and `pm_sd_mkdir()` are hand-written host
primitives in `pm_app_api.h`, backed by raw `File::write` and `FS::mkdir`, and
`pm_sd_read_binary_file()` reads back arbitrary sizes.

A function returning `std::vector<String>`, such as word wrapping, becomes a
count call and an indexed getter, matching how the SDK already hands out scan
results:

```cpp
char line[80];
int lines = pm_layout_word_wrap_count(text, maxWidth, PM_STYLE_BODY);
for (int i = 0; i < lines; i++) {
  pm_layout_word_wrap_get(text, maxWidth, PM_STYLE_BODY, i, line, sizeof line);
  draw(line);
}
```

The getter writes into a buffer the caller owns rather than the string pool,
because a wrapped paragraph can be far longer than the pool holds, and it
returns `-1` for an out of range index. A function taking
`const std::vector<String>&` takes a C array and a count instead:
`pm_io_join_string(const char* const* items, int count, char delimiter)`.

The generated wrappers are exported through `symbols.list`, so an app that links
against `pm_sdk_app.h` resolves every symbol it names. A new wrapper belongs in
that list before any app can call it; a name that is missing fails at load with
`Can't find common <name>` rather than at link time.

## SDK version stamp

The host exports the ABI version as a string symbol, defined in
`pocketmage_globals.cpp` and curated in `symbols.list`:

```cpp
extern "C" const char pocketmage_sdk_version[];  // e.g. "0.1.0"
```

Apps consume it through the macro surface in `pocketmage_app_version.h`:
`POCKETMAGE_SDK_VERSION_STRING`, `POCKETMAGE_SDK_VERSION_MAJOR`,
`POCKETMAGE_SDK_VERSION_MINOR`, `POCKETMAGE_SDK_VERSION_PATCH`. The firmware
loads these macros from `-DPM_SDK_VERSION_*` supplied by `app.mk`, and the
OS's loader export table (generated from `symbols.list` via `tools/symbols.py`)
must contain the matching string symbol.

Keeping VERSION, the string literal, the header macros, and `library.json` in
sync is enforced by the CI `version-sync` job and `pm release`, which
rewrites all four together.

## Runtime environment

- The app shares the host's heap. Loading costs roughly the ELF size plus a
  fixed reserve; `runElfApp` refuses to launch when free `MALLOC_CAP_8BIT`
  drops below that floor. Keep the app small.
- The app runs in-process. It can call anything the host exports - the
  display, keyboard stack, translation engine, peripheral drivers - at native
  speed, no IPC.
- Stack is not heap. The run task gives 16 KB; deep recursion or fat frames
  blow it. Move working sets to the heap.
- E-ink refreshes use the differential partial waveform while an app runs,
  regardless of the device's FAST_REFRESH setting; the setting is restored
  when the app exits. Redraw on change, not on a timer: even partial
  updates are visible, and a periodic slow clean still flashes the panel.

## Sections and strip

`app.mk` links with `--gc-sections` (functions and data) and strips
loader-irrelevant sections, so the delivered ELF comes down to `.text`,
`.rodata`, `.got`, `.rela.dyn`, `.rela.plt`, `.dynsym`, `.dynstr`, `.hash`,
`.shstrtab`. Everything else is removed. Code that depends on other sections
existing is unsupported. Don't write it.

## App manifest (`app.properties`)

When loading an app from SD, the host reads `app.properties` from the app directory (max 1024 bytes). Recognized fields:

- `name` – human-readable app name
- `version` – app version string
- `author` – author string, free form; multiple authors may be comma
  separated (spaces are kept, up to 31 bytes)
- `scope` – symbol scope gate (default: `all`)

Only `pm_*` symbols are scope-gated. libc, libstdc++, and ESP-IDF symbols remain available regardless of scope. The loader installs the scoped resolver before relocation and resets it on exit. argv metadata passed to `app_main` includes `name`, `elfPath`, `version`, and `author`.

## Endianness failure modes

| Symptom at load                              | Cause                               |
| -------------------------------------------- | ----------------------------------- |
| garbage / instruction fetch fault in IRAM    | ELF fields decoded wrong (BE)       |
| `esp_elf_load_section` fault                 | misaligned / incorrect section data |
| load "succeeds", entry behaves wildly        | wrong sizes from BE headers         |

The build-time guard catches BE before it reaches a device: if the last
`make` line is `error: ... big-endian ...`, rebuild with the espressif
toolchain (see [build.md](build.md)).

## Stability rules for SDK authors

- The format, entry point, and load strategy are public contract. Changing how
  an app maps or invokes takes a `major` bump of `pocketmage_sdk_version` and
  a migration note.
- Exports are additive. Removing or changing an export breaks live apps. See
  [CONTRIBUTING.md](../CONTRIBUTING.md).
