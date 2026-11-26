---
type: api
title: "Logging (pocketmage_log)"
description: "The SDK logger: levels, per-tag filtering, the serial console, the optional log file, and the log tail kept for crash reports."
source: "https://talismandesign.github.io/PocketMage_SDK/docs/api/log/"
path: /api/log/
updated: 2026-11-26
okf:
  generated_by: "@docmd/plugin-okf"
  generated_at: "2026-11-26T04:24:34.154Z"
---
---
title: "Logging (pocketmage_log)"
description: "The SDK logger: levels, per-tag filtering, the serial console, the optional log file, and the log tail kept for crash reports."
---

# Logging API

`pm_log` is the one place PocketMage writes log messages. Every line goes to the serial console at 115200 baud and into a small buffer in RAM that crash reports include. When the **Log to File** setting is on, lines are also saved to the SD card, so you can read what happened without a computer attached.

A line looks like this:

```log
48213 W [AUDIO] USB speaker not ready, retrying
```

That is, the uptime in milliseconds, the level letter (`E`, `W`, `I`, `D` or `V`), the tag in brackets, and the message.

## Access

There is one way to write a log line, and it is the same in the OS, the SDK and ELF apps, from C or C++:

```cpp
#include <pocketmage.h>    // OS and SDK code
#include <pm_app_api.h>    // ELF apps

static constexpr const char* TAG = "NOTES";

PM_LOGI(TAG, "Opened %s", path);
PM_LOGE(TAG, "Save failed: %d", err);
```

Both includes bring in `pocketmage_log/log_api.h`, which holds the macros and the one function behind them. `pm_log::begin()` and `pm_log::setFileEnabled()` already run inside `PocketMage_INIT()`, so code only writes messages.

ESP-IDF components (Wi-Fi, the USB host stack, the FAT driver and so on) send their own log lines through the logger as well, keeping their tag and level. OS and SDK code uses `PM_LOGx`; raw `Serial`/`printf` and IDF early/DRAM logging bypass the hook and stay console-only.

## Writing messages

```c
PM_LOGE(tag, format, ...);
PM_LOGW(tag, format, ...);
PM_LOGI(tag, format, ...);
PM_LOGD(tag, format, ...);
PM_LOGV(tag, format, ...);
```

Each macro formats like `printf` and logs at its level: Error, Warn, Info, Debug or Verbose. A higher level means more detail.

A macro above `PM_LOG_MAX_LEVEL` compiles to nothing, so its arguments are not even evaluated. All five levels are compiled in by default; define `PM_LOG_MAX_LEVEL` before the include to drop the noisier ones from one file or a whole build. Every level that is compiled in is then filtered per tag at run time; see `setLevel` below.

Messages longer than 255 bytes are cut. Line breaks at the end are removed, and other control characters become spaces, so one call always makes one line.

### Levels

```c
enum pm_log_level {
  PM_LOG_LEVEL_NONE = 0,
  PM_LOG_LEVEL_ERROR = 1,
  PM_LOG_LEVEL_WARN = 2,
  PM_LOG_LEVEL_INFO = 3,
  PM_LOG_LEVEL_DEBUG = 4,
  PM_LOG_LEVEL_VERBOSE = 5,
};
```

The values match ESP-IDF's `esp_log_level_t`. `PM_LOG_LEVEL_NONE` is only used to turn a tag off with `setLevel` and is never written.

## Controlling output

These live in the `pm_log` namespace and are for OS and SDK code. They decide where lines go and which ones are kept; none of them write a message.

```cpp
namespace pm_log {
void begin();
void setFileEnabled(bool enabled);
bool fileEnabled();
bool setLevel(const char* tag, pm_log_level level);
pm_log_level level(const char* tag);
bool flush(uint32_t timeoutMs);
size_t copyTail(char* out, size_t capacity);
}
```

### `begin`

Routes ESP-IDF logs through the logger and makes sure queued lines are saved when the device restarts. Calling it again does nothing. `PocketMage_INIT()` calls it right after `Serial.begin`.

### `setFileEnabled`

Turns saving to the SD card on or off. The first time it is turned on, a small background task named `pm_log` starts; if it cannot start, file output stays off and an error is logged. Turning file output off throws away lines that were not saved yet.

Lines logged before the first call are held in memory, so the boot messages that come before the SD card is ready still reach the file. `PocketMage_INIT()` calls it with the `LOG_TO_FILE` setting once the card is set up, and the Settings app calls it again when you change the setting.

### `fileEnabled`

Returns whether file output is on.

### `setLevel`

Sets the most detailed level written for one tag, for example `pm_log::setLevel("USB", PM_LOG_LEVEL_VERBOSE)` while you chase a USB problem. A null tag or `"*"` changes the default used by every tag without its own setting. The default starts at Info. The same level is passed on to ESP-IDF, so its components with that tag follow along.

Up to 24 tags can have their own level. Returns false for a value that is not a level, an empty tag, a tag longer than 15 characters, or when all 24 slots are taken. Levels are kept in RAM only and reset to Info on every boot.

### `level`

Returns the most detailed level written for a tag, or the default when the tag has no setting of its own.

### `flush`

Waits up to `timeoutMs` for the queued lines to reach the card. Returns true once everything is written. Returns false straight away when file output is off, the card is missing or in USB file transfer mode, or when called from the writer task itself. The SDK calls it before deep sleep, because deep sleep skips the restart handlers.

### `copyTail`

Copies the newest log text (up to 1 KB) into `out`, oldest line first, and ends it with NUL. It starts on a whole line and takes no locks, so the crash handler can use it. Returns the length without the NUL. You rarely need this; crash reports already include it.

## Log file

| File                  | What it is                                            |
| --------------------- | ----------------------------------------------------- |
| `/logs/system.txt`    | The current log                                       |
| `/logs/system.1.txt`  | The previous log, replaced each time the file rotates |

File output is off by default. Turn on **Settings > Log to File** (the `LOG_TO_FILE` preference, see [configuration](configuration.md)) to start saving.

When `system.txt` would grow past 256 KB, it is renamed to `system.1.txt` (replacing the older copy) and a new file is started. So the card holds at most about 512 KB of logs.

Lines are saved in batches about once a second. A warning or an error, or a queue that is half full, saves at once. The queue holds 4 KB; if the card cannot keep up, new lines are dropped and the file shows `-- N log lines dropped --` where they would have been. If a write fails, the writer tries again on its next one-second pass. While the card is shared over USB file transfer, lines wait in the queue.

Crash reports are separate and are always saved, to `/logs/crash/`. Each report ends with the last 1 KB of log lines, even when file output is off. See the OS's crash log guide for how to read them.

## Threading

The `PM_LOGx` macros are safe from any task and from interrupt handlers. Each line is written whole, so lines from different tasks never mix. The console output is synchronous and takes about as long as any serial print; saving to the card always happens later on the `pm_log` task (priority 1, core 1), so logging never waits on the SD card.

Call `setLevel`, `setFileEnabled` and `flush` from tasks only, never from an interrupt.

## Example

```cpp
#include <pocketmage.h>

static constexpr const char* TAG = "SYNC";

bool syncNotes() {
  PM_LOGI(TAG, "Sync started");
  pm_log::setLevel(TAG, PM_LOG_LEVEL_DEBUG);
  for (int attempt = 1; attempt <= 3; ++attempt) {
    PM_LOGD(TAG, "Attempt %d", attempt);
    if (uploadNotes())
      return true;
    PM_LOGW(TAG, "Attempt %d failed", attempt);
  }
  PM_LOGE(TAG, "Sync gave up after 3 attempts");
  return false;
}
```

## C ABI (`pm_*`)

ELF apps use the same `PM_LOGx` macros through `pm_app_api.h`. The one function behind them is exported to apps:

```c
void pm_log_write(int level, const char *tag, const char *format, ...);
```

- `pm_log_write(level, tag, format, ...)` formats the message on the OS side and logs it under `tag`.
  - `level` is one of `PM_LOG_LEVEL_ERROR` to `PM_LOG_LEVEL_VERBOSE`. Any other value is ignored.
  - `tag` is the tag shown in brackets; null gives an empty tag.
  - `format` and the arguments after it work like `printf`. A null `format` is ignored.

Call it directly only when the level is chosen at run time; otherwise use the macros, which also drop lines above `PM_LOG_MAX_LEVEL` at compile time. The `pm_log::` control functions are not exported; apps cannot change levels or file output.

## Limits

- Arduino's `log_d` and `log_v` compile out entirely at `CORE_DEBUG_LEVEL=3` (the PocketMage build's level), so `PM_LOGD`/`PM_LOGV` are the way to get Debug and Verbose lines.
- `isr_log_x`, `ESP_EARLY_LOGx` and `ESP_DRAM_LOGx`, and raw `Serial`/`printf` calls (GxEPD2, MP2722, LibSSH, PNGdec, wrench, usb_stream descriptor dumps) bypass the hook and stay console-only.
- `PM_LOGx` from an interrupt only buffers in RAM; the `pm_log` writer task does the file write later. Use integer-only formats in ISRs: the ISR stack is 2096 bytes and the format buffer is 256 bytes.
- Do not call IDF v1 `ESP_LOGx` from an interrupt handler. IDF filters the tag before calling our hook, and the call is not ISR-safe.
- ESP-IDF's `esp_log_level_set("*", x)` clears every per-tag entry, so `setLevel` sets the default first and replays the stored entries (serialized by a mutex).
- `ESP_LOGD` strings are compiled out unless IDF's tag level allows Debug. `LOG_LOCAL_LEVEL=3` in `build_flags` lets IDF-compiled libraries emit Info lines cheaply. Full IDF Debug coverage needs `CONFIG_LOG_MAXIMUM_LEVEL=4` and a full IDF rebuild; it is planned for the future some day (when that is, who knows).
