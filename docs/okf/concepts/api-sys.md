---
type: api
title: "System (pocketmage_sys, pocketmage_globals)"
description: "Peripheral init, CPU speed, deep sleep, crash recovery, and the shared global state."
source: "https://talismandesign.github.io/PocketMage_SDK/docs/api/sys/"
path: /api/sys/
updated: 2026-10-06
okf:
  generated_by: "@docmd/plugin-okf"
  generated_at: "2026-10-06T07:52:17.224Z"
---
---
title: "System (pocketmage_sys, pocketmage_globals)"
description: "Peripheral init, CPU speed, deep sleep, crash recovery, and the shared global state."
---

# System API

The system module owns the board bring-up, the CPU clock, deep sleep, the power button, and the shared globals (`battState`, `allLines`, `CurrentAppState`, the SPI buses, and NVS `prefs`). Apps use it for power management and boot-time configuration; the OS uses it for full bring-up.

## Access

```cpp
#include <pocketmage.h>
```

## Shared globals

`pocketmage_globals` defines the state both the OS and apps link against.

### SPI and filesystem

```cpp
extern SPIClass* vspi;    // the global SPI on SPI_SCK/SPI_MOSI
extern SPIClass* hspi;
extern fs::FS*   global_fs;  // active filesystem (SD_MMC or SD)
```

`PocketMage_INIT` wires `vspi = &SPI` and pins the pins documented in [configuration](configuration.md).

### NVS

```cpp
extern Preferences prefs;   // "PocketMage" namespace
```

The shared Preferences handle. `pocketmage::loadSettings()` reads the persistent toggles into the `config.h` globals (`TIMEOUT`, `SAVE_POWER`, `FAST_REFRESH`, `OLED_BRIGHTNESS`, the UI language, ...); an app that reads preferences uses `prefs` directly with the same namespace.

### App state

```cpp
enum AppState { HOME, TXT, FILEWIZ, USB_APP, COMM, SETTINGS, TASKS,
                CALENDAR, JOURNAL, LEXICON, APPLOADER, TERMINAL,
                ONBOARDING, ELFAPP };
extern AppState CurrentAppState;
```

The OS's app dispatch enum. `ELFAPP` is the state set while a loaded ELF app is running; the OS restores `HOME` when it exits.

### Keyboard state

```cpp
enum KBState { NORMAL, SHIFT, FUNC, FN_SHIFT };
```

The keyboard modifier state, mirrored by `KB().setKeyboardState()`.

### `pocketmage::` namespace

```cpp
namespace pocketmage {
  void loadSettings();          // load NVS settings into the config.h globals
  void recoverFromCrash();      // reset app state after a panic/watchdog
  void checkRTCPowerLoss();     // baseline the RTC when it has lost power
}
```

- `loadSettings()` reads every persistent toggle from `prefs` and sets the UI language. Apps call it during `APP_INIT` (the OS reaches it via `PocketMage_INIT` on the host path).
- `recoverFromCrash()` checks the ESP reset reason and, on panic or watchdog reset, resets `CurrentAppState` to `HOME` and clears the editing-file pointer so the device boots clean.
- `checkRTCPowerLoss()` re-baselines the RTC when its power was lost.

### Battery helper

```cpp
inline float getBatteryVoltage() {
  return (analogRead(BAT_SENS) * (3.3 / 4095.0) * 2) + 0.2;
}
```

`battState` (0 through 5) indexes the battery bitmaps used by the OLED status line ([oled](oled.md), [assets](assets.md)).

## CPU speed

```cpp
void pocketmage::setCpuSpeed(int newFreq);
```

Clamps to the valid S3 frequencies `{240, 160, 80, 40, 20, 10}` and returns early if the CPU is already there. WiFi stalls below 240 MHz on the S3, so a request that would drop the clock below `WIFI_CPU_FREQ_MHZ` (240) while `P_WIFI` is not `Off` is ignored. `POWER_SAVE_FREQ` (40) is the OS's idle speed; `sysMessage` and the WiFi service bump back to 240 when they need it.

### Scope guard

```cpp
class pocketmage::ScopedCpuBoost {
public:
  ScopedCpuBoost();            // raises to 240 MHz if not already there
  ~ScopedCpuBoost();           // restores the previous frequency
};
```

Non-copyable RAII. Usable as a stack local around a latency-critical section.

## Deep sleep

```cpp
void pocketmage::deepSleep(bool alternateScreenSaver = false);
```

Full power-down sequence:

1. Power-saves the OLED.
2. Stops the `einkHandlerTask` under the panel lock, so an in-flight refresh completes.
3. Plays `Jingles::Shutdown`.
4. Draws a screensaver (custom `/assets/backgrounds/*.bin`, else a random built-in) at rotation 3, or the alternate screensaver when asked. `SAVE_POWER` drops the CPU to `POWER_SAVE_FREQ` for the draw.
5. Clears the buffer, hibernates the panel, unlocks it.
6. Persists `CurrentAppState`, the editing file, and clears the seamless reboot flag.
7. On production hardware, isolates the peripheral pins with `setLoadSwitch(false)` (GPIO hold + `gpio_deep_sleep_hold_en`).
8. `esp_deep_sleep_start()`.

Wake is on the `KB_IRQ` falling edge (configured in `PocketMage_INIT`). Resume re-runs `PocketMage_INIT`, which clears the pin holds and re-inits peripherals.

## Power button

```cpp
void IRAM_ATTR pocketmage::PWR_BTN_irq();
extern volatile bool PWR_BTN_event;
```

The FALLING-edge interrupt service routine sets `PWR_BTN_event`. The OS consumes it for wake/shutdown handling; holding `PWR_BTN` for 3 seconds while awake raises the hard-reset-to-home path (`resetRequested`, see the `hardReset` task). `SDActive`, `mscEnabled`, and `sinkEnabled` are the status-line indicators apps toggle for long I/O.

## Bring-up

```cpp
void PocketMage_INIT();
```

The one-call bring-up: release GPIO holds, enable the load switch (production), read the seamless reboot flag, `FontEngine::init()`, Serial, Wire (plus Wire1 on production), the global SPI, the keyboard wake pin, `setupOled()`, `setupKB()`, `setupEink()`, `setupSD()`, power-button interrupt + MP2722 power init, the hard-reset task, the power-save clock, `setupTouch()`, `setupClock()`, the random seed, settings load, crash recovery, `setupBZ()` + startup jingle, the WiFi service (host only), key flush, then the app hook `APP_INIT()` (app builds). Apps do not call it; it runs as part of the OS or the app entry shim.

## Text line buffer

```cpp
extern volatile bool resetRequested;
extern volatile bool newLineAdded;
extern std::vector<String> allLines;   // the text editor's line list
extern bool noTimeout;
extern void setLoadSwitch(bool state);
```

`allLines` is the shared text buffer the OS's TXT flow and `vectorToString` / `stringToVector` operate on; `noTimeout` suppresses the auto-sleep timeout. `setLoadSwitch(false)` is the production deep-sleep pin isolation path described above.

## Example

```cpp
#include <pocketmage.h>

void onUserExit() {
  {
    pocketmage::ScopedCpuBoost boost;   // 240 MHz for the save
    // write the document...
  }  // back to the previous speed
  pocketmage::deepSleep();              // and go to sleep
}
