---
title: "App lifecycle"
description: "Entry points, task-owned resources, idle behavior, and resident app finalization."
---

# App lifecycle

A PocketMage app is a plugin that runs *inside* the host firmware's address space. Understanding where the app boundaries are is the whole battle: the app is a normal `main`, everything it touches comes from the host, and "exiting" is a `return`.

## The entry point

The ELF entry is `app_main`, produced by the build recipe's `-Dmain=app_main -e app_main`. Because an app is truly a freestanding ELF, the canonical program name is kept by writing:

```cpp
extern "C" int main(int argc, char** argv)
```

with `main` linked to `app_main`. `argc`/`argv` are the invocation arguments the OS passes. The Arduino-style `setup()`/`loop()` runtime is layered on top of this entry by the SDK's app-runtime shim, so apps that prefer that shape keep the same export.

## Who owns the world

- The app owns its code, its static data, and its calls into the host.
- The host owns hardware, SDK singletons, the shared heap, and the task running the app.
- The app links its own source and the SDK's small exit-hook bridge. `-nostdlib` leaves runtime and SDK references for the loader to resolve through its enabled export tables (see [symbols](../symbols.md)).

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
- Idle timeout belongs to the OS for both native and ELF apps. Keyboard/touch input and `pm_activity()` reset the shared clock. Playback, an attached USB speaker, open SD streams, transfer mode, and timeout-inhibition flags keep the device awake. Use the configured `TIMEOUT` rather than an app-local sleep timer.

The app owns its loop while the OS continues accessory and idle polling. Yield with `delay()` between frames. Deep sleep first checks audio and keyboard shutdown; if cleanup is incomplete, the device remains awake and displays the existing USB failure message.

## Exiting

```cpp
return 0;   // plain exit; control returns to the OS cleanly
```

Return from `main` to exit. Before releasing executable memory, the runner:

1. Waits for checked audio shutdown, retaining the image if a worker has not stopped.
2. Runs the ELF finalizer arrays while the base image and overlay remain resident.
3. Drains the task's registered exit callbacks and C++ destructors in reverse registration order. Callbacks added during finalization are drained as well.
4. Stops any audio restarted by finalizers, then closes only that app task's SD streams and directory snapshot.
5. Unloads the overlay, deallocates the ELF, restores HOME, and repaints.

`tools/runtime/exit.cpp`, linked by `tools/app.mk`, routes `atexit`, `__cxa_atexit`, and `__cxa_finalize` to the app session. `pm_app_at_exit()` is the explicit C entry point. The registry has 32 pending slots; registration returns `-1` for a null callback, a full/inactive registry, or a caller other than the app task. A callback is removed before invocation, so recursive finalization cannot run it twice. Firmware-task registrations are not attached to an app.

The base ELF's SD stream closes after relocation. Overlay reads use logical handles and short-lived gateway streams, so an otherwise idle app does not keep a stream open merely because its code came from SD. Apps must still stop their own background work before returning; the runner cannot infer ownership of arbitrary app-created tasks.

The local `finalizeElf()` bridge follows the pinned loader's finalizer order and clears `init_done` before callbacks. [Espressif PR #842](https://github.com/espressif/esp-iot-solution/pull/842) proposes `esp_elf_finalize()` for this split between finalization and deallocation. Until it is available in the pinned dependency, the bridge remains in the OS.

A fault or watchdog reset uses the OS [crash recovery](../api/sys.md) path and boots back to HOME. Normal exit callbacks are not guaranteed on a reset.

## Built-in vs third-party

The OS's HOME, TXT, and other built-in apps use native SDK services. ELF apps use generated C wrappers over those same services. Validation, audio preferences, media parsing, key codes, canvas behavior, and idle policy have one implementation; ABI 2 compatibility adapters preserve existing ELF call signatures.
