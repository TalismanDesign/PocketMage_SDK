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

So apps link `-nostdlib`: no newlib, no libstdc++, no SDK copy in the ELF.
Everything an app references must resolve at load time from the host. Linking
newlib into the app is wasteful and collides with host globals. Don't.

## Runtime environment

- The app shares the host's heap. Loading costs roughly the ELF size plus a
  fixed reserve; `runElfApp` refuses to launch when free `MALLOC_CAP_8BIT`
  drops below that floor. Keep the app small.
- The app runs in-process. It can call anything the host exports - the
  display, keyboard stack, translation engine, peripheral drivers - at native
  speed, no IPC.
- Stack is not heap. The run task gives 16 KB; deep recursion or fat frames
  blow it. Move working sets to the heap.

## Sections and strip

`app.mk` links with `--gc-sections` (functions and data) and strips
loader-irrelevant sections, so the delivered ELF comes down to `.text`,
`.rodata`, `.got`, `.rela.dyn`, `.rela.plt`, `.dynsym`, `.dynstr`, `.hash`,
`.shstrtab`. Everything else is removed. Code that depends on other sections
existing is unsupported. Don't write it.

## Endianness failure modes

| Symptom at load                              | Cause                             |
| -------------------------------------------- | --------------------------------- |
| garbage / instruction fetch fault in IRAM    | ELF fields decoded wrong (BE)     |
| `esp_elf_load_section` fault                 | misaligned / incorrect section data |
| load "succeeds", entry behaves wildly        | wrong sizes from BE headers       |

The build-time guard catches BE before it reaches a device: if the last
`make` line is `error: ... big-endian ...`, rebuild with the espressif
toolchain (see [build.md](build.md)).

## Stability rules for SDK authors

- The format, entry point, and load strategy are public contract. Changing how
  an app maps or invokes takes a `major` bump of `pocketmage_sdk_version` and
  a migration note.
- Exports are additive. Removing or changing an export breaks live apps. See
  [CONTRIBUTING.md](../CONTRIBUTING.md).