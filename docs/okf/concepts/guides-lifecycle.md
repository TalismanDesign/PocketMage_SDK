---
type: guide
title: "App lifecycle"
description: "Entry points, build-time vs host symbols, the APP_INIT hook, and how an app exits."
source: "https://talismandesign.github.io/PocketMage_SDK/docs/guides/lifecycle/"
path: /guides/lifecycle/
updated: 2026-10-05
okf:
  generated_by: "@docmd/plugin-okf"
  generated_at: "2026-10-05T01:48:20.788Z"
---
---
title: "App lifecycle"
description: "Entry points, build-time vs host symbols, the APP_INIT hook, and how an app exits."
---

# App lifecycle

A PocketMage app is a plugin that runs *inside* the host firmware's address space. Understanding where the app boundaries are is the whole battle: the app is a normal `main`, everything it touches comes from the host, and "exiting" is just `return`.

## The entry point

The ELF entry is `app_main`, produced by the build recipe's `-Dmain=app_main -e app_main`. Because an app is truly a freestanding ELF, the canonical program name is kept by writing:

```cpp
extern "C" int main(int argc, char** argv)
```

with `main` linked to `app_main`. `argc`/`argv` are the invocation arguments the OS passes. The Arduino-style `setup()`/`loop()` runtime is layered on top of this entry by the SDK's app-runtime shim, so apps that prefer that shape keep the same export.

## Who owns the world

- The app owns its code, its static data, and its calls into the host.
- The host owns all hardware, all SDK singletons, the memory manager the app allocates from, and the thread that runs the app's entry nightly.
- Nothing is statically linked into the app (`-nostdlib`, undefined references resolved at load). Any symbol the app uses that is not defined inside it must be in the host export table, or load fails the `pm check` surface (see [symbols](../symbols.md)).

Build-time version macros (`PM_SDK_VERSION_*`) are baked into the app; the host exports the matching `pocketmage_sdk_version` string/runtime parts it was built against. An app compares its own compile-time version against the host's export to present a compatible pair.

## `APP_INIT`

Apps receive one startup hook:

```cpp
void APP_INIT();   // defined by the app, invoked by the entry shim
```

The OS/SDK host reaches `APP_INIT()` after peripheral and singleton bring-up (displays, FS, clock, i18n, ...), so an app may read settings, open files, and prep its first screen there. The hook is also where boot-time apps decide whether to relaunch their UI at power-on.

## Interactive lifetime

While running, the app owns the loop:

- Poll input: `pm_kb_read()` for a keypress, `pm_kb_state()` for the current modifier state.
- Draw: build the E-Ink buffer, refresh once, draw OLED text.
- Sleep on idle: mirror the OS's `CLOCK().setPrevTimeMillis(millis())` poke or call `pocketmage::deepSleep()` explicitly.

There is no cooperative yield handshake: the app either returns, sleeps, or keeps its own loop going. A CPU-heavy loop starves the WiFi and E-Ink tasks; prefer `pocketmage::ScopedCpuBoost` for short bursts and yield with `delay()` between frames.

## Exiting

```cpp
return 0;   // plain exit; control returns to the OS cleanly
```

Exiting is just `return` from `main`. There is no reboot handoff and no exit API. The loader tears down the app context, restores `HOME`, and repaints. A crashing app (fault, uncaught aborts) follows the same OS recovery path as the built-in apps ([`recoverFromCrash`](../api/sys.md)); the device boots back to HOME rather than leaving the app stuck.

## Built-in vs third-party

The OS's `HOME`, TXT, and friends are compiled-in apps that live entirely on the host side. Third-party apps are the ELF path described here. Both inhabit the same `lib/PocketMage_SDK` API, so the same app code
