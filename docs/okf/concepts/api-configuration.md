---
type: api
title: Configuration
description: "Compile-time pins and knobs in config.h, persistent preference globals, and how an app sees the SDK version."
source: "https://talismandesign.github.io/PocketMage_SDK/docs/api/configuration/"
path: /api/configuration/
updated: 2026-10-08
okf:
  generated_by: "@docmd/plugin-okf"
  generated_at: "2026-10-08T10:14:20.966Z"
---
---
title: "Configuration"
description: "Compile-time pins and knobs in config.h, persistent preference globals, and how an app sees the SDK version."
---

# Configuration

`config.h` holds the compile-time hardware map and tuning knobs for the SDK. It is included by the umbrella header, so no extra include is needed. Apps read these constants; only firmware builds may change their values.

## Compile target

```c
#if defined(PM_TARGET_HOST) && defined(PM_TARGET_APP)
#error "PM_TARGET_HOST and PM_TARGET_APP are mutually exclusive"
#endif
#if !defined(PM_TARGET_HOST) && !defined(PM_TARGET_APP)
#error "Define PM_TARGET_HOST (OS firmware) or PM_TARGET_APP (external .app.elf)"
#endif
```

Define exactly one. The preprocessor enforces it with a hard error.

## Tuning knobs

| Constant                 | Default | Meaning                                    |
| ------------------------ | ------- | ------------------------------------------ |
| `KB_COOLDOWN`            | 3       | keypress cooldown (ms)                     |
| `MAX_FILES`              | 10      | file slots in the SD file list             |
| `FORMAT_SPIFFS_IF_FAILED`| `true`  | format SPIFFS on mount failure             |
| `SLEEPMODE`              | `"TEXT"`| sleep screen: TEXT, SPLASH, or CLOCK        |
| `TXT_APP_STYLE`          | 1       | TXT renderer: 1 = current, 0 = unsupported |
| `SET_CLOCK_ON_UPLOAD`    | `false` | set the clock from compile time on upload  |
| `TOUCH_TIMEOUT_MS`       | 1200    | ms before scrolling returns to typing      |
| `SYS_METADATA_FILE`      | `"/sys/SDMMC_META.txt"` | filesystem metadata path |
| `POWER_SAVE_FREQ`        | 40      | CPU MHz when power saving                  |
| `IDLE_TIME`              | 20000   | ms of idle before sleep                    |
| `FAST_REFRESH_AFTER`     | 5       | fast partial refreshes before a clean      |
| `FULL_REFRESH_AFTER`     | 5 or 10 | full refresh cadence (5 beta, 10 prod)     |

`FULL_REFRESH_AFTER` is hardware-dependent: production (`POCKETMAGE_HW_VERSION == 2`) uses 10, beta uses 5.

## Pin map

| Group        | Constants   | Notes                                    |
| ------------ | ----------- | ---------------------------------------- |
| I2C          | `I2C_SDA=36`, `I2C_SCL=35` | primary bus: OLED, clock              |
| Switched I2C | `SWITCHED_I2C_SDA=42`, `SWITCHED_I2C_SCL=48` | production only: touch, expansion |
| USB mux      | `USB_MUX_PIN=7` |                                           |
| Keyboard     | `KB_IRQ=8`  | wake source, falling edge                 |
| Power        | `PWR_BTN=0`, `BAT_SENS=4`, `CHRG_SENS=39`, `LOAD_SWITCH=38` |  |
| RTC          | `RTC_INT=1` |                                            |
| OLED         | `OLED_CS=47`, `OLED_DC=46`, `OLED_RST=45` | SPI                          |
| E-Ink        | `EPD_CS=2`, `EPD_DC=21`, `EPD_RST=9`, `EPD_BUSY=37` | SPI                |
| SDMMC        | `SD_CLK=12`, `SD_CMD=11`, `SD_D0..D3=13,5,6,10` | 4-bit bus          |
| SDSPI        | `SD_CS=10`, `SD_MOSI=11`, `SD_SCK=12`, `SD_MISO=13` | fallback mode   |
| SPI shared   | `SPI_MOSI=14`, `SPI_SCK=15` | the SPI object both display drivers use |
| Buzzer       | `BZ_PIN=17` | PWM output                                 |

The SPI pins overlap the SDMMC pins (`SD_CLK` is also `SPI_SCK`). The two SD modes share the same physical pins by design; see [sd](sd.md) for the mode switch. `PocketMage_INIT` uses a single global `SPI` instance wired to `SPI_SCK`/`SPI_MOSI` precisely so display and SDSPI do not fight over GPIO12.

## Persistent preferences

`config.h` declares the runtime settings stored in NVS under the `"PocketMage"` namespace. The SDK owns their defaults; apps read and write them with `prefs` (also in [globals](sys.md)):

```c
extern int TIMEOUT;          // auto sleep timeout, seconds
extern bool DEBUG_VERBOSE;   // extra debug output
extern bool SYSTEM_CLOCK;    // show clock on the OLED status line
extern bool SHOW_YEAR;       // show year in clock
extern bool SAVE_POWER;      // CPU frequency power saving
extern bool ALLOW_NO_MICROSD;
extern bool HOME_ON_BOOT;
extern bool FAST_REFRESH;    // experimental fast partial refresh
extern int OLED_BRIGHTNESS;  // 0..255
extern int OLED_MAX_FPS;
extern bool MUTE_BUZZER;
extern bool SD_SPI_COMPATIBILITY;
```

`pocketmage::loadSettings()` (see [sys](sys.md)) loads them from NVS with these defaults. Changing a value writes it back through `prefs.begin(...)` in the `"PocketMage"` namespace.

## App version stamping

Apps built with `tools/app.mk` get `PM_SDK_VERSION` plus `PM_SDK_VERSION_MAJOR/MINOR/PATCH` on the command line from the repo `VERSION` file. `pocketmage_app_version.h` exposes them as macros:

```c
POCKETMAGE_SDK_VERSION_STRING   // "0.1.0"
POCKETMAGE_SDK_VERSION_MAJOR
POCKETMAGE_SDK_VERSION_MINOR
POCKETMAGE_SDK_VERSION_PATCH
```

The host exports the same version as the `pocketmage_sdk_version` string symbol, so an app can compare its build-time SDK with the running OS at load time (see [app-abi](../app-abi.md) and version_app in
