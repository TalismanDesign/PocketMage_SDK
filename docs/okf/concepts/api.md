---
type: api
title: "API Reference"
description: "Every component an app links against: the umbrella header, the singleton accessors, and the per-component pages."
source: "https://talismandesign.github.io/PocketMage_SDK/docs/api/"
path: /api/
updated: 2026-10-10
okf:
  generated_by: "@docmd/plugin-okf"
  generated_at: "2026-10-10T16:00:43.161Z"
---
---
title: "API Reference"
description: "Every component an app links against: the umbrella header, the singleton accessors, and the per-component pages."
---

# API Reference

The SDK exposes the PocketMage hardware and OS services to loaded apps through a set of component objects plus free functions. Every component is declared in `pocketmage.h`, the umbrella header an app includes first:

```cpp
#include <pocketmage.h>
```

`pocketmage.h` pulls in every component header, `config.h`, the `frames` and `assets` modules, and `Preferences.h`. It also includes `pocketmage_app_version.h` when the build defines `PM_TARGET_APP`, so the SDK-version macros are always available to apps.

## Compile target

`config.h` requires exactly one of these to be defined, and fails the build otherwise:

| Macro           | Meaning                                                       |
| --------------- | ------------------------------------------------------------- |
| `PM_TARGET_HOST` | the PocketMageOS firmware links the SDK in-process            |
| `PM_TARGET_APP`  | an external app compiled to a loadable `.app.elf`            |

The two are mutually exclusive; defining both is a build error. Apps declare `PM_TARGET_APP` (see [Building an app](../build.md)).

## The singleton pattern

Most components are owned by a single global instance created inside the SDK and reached through a global accessor function. The accessor returns a reference; null checks are unnecessary once the component's `setup*()` call has run in `PocketMage_INIT()`.

| Component   | Accessor    | Hardware / role                                  | API page            |
| ----------- | ----------- | ------------------------------------------------ | ------------------- |
| OLED        | `OLED()`    | 256x32 SSD1326 OLED, status line                  | [oled](oled.md)     |
| E-Ink       | `EINK()`    | 320x240 GDEQ031T10 panel, full-page rendering     | [eink](eink.md)     |
| Font        | `FontEngine` | unified text on both displays                     | [font](font.md)     |
| Layout      | (free)      | display geometry constants, text fit helpers      | [layout](layout.md) |
| UI helpers  | (free)      | status bars, list rows, chips                     | [ui](ui.md)         |
| SD card     | `PM_SDAUTO()` | files, metadata, dos/flash filesystem             | [sd](sd.md)         |
| Keyboard    | `KB()`      | TCA8418 matrix + USB HID                           | [kb](kb.md)         |
| Touch       | `TOUCH()`   | MPR121 slider, scroll gestures                     | [touch](touch.md)   |
| Buzzer      | `BZ()`      | PWM buzzer, jingles                               | [bz](bz.md)         |
| Clock       | `CLOCK()`   | PCF8563 RTC, timeout bookkeeping                   | [clock](clock.md)   |
| WiFi        | `P_WIFI`    | station radio service (apps may call)              | [wifi](wifi.md)     |
| System      | (free)      | init, CPU speed, deep sleep                        | [sys](sys.md)       |
| I18n        | `I18n`      | language tables, command normalization             | [i18n-runtime](i18n-runtime.md) |
| Text utils  | (free)      | string split/join/parse helpers                    | [io](io.md)         |
| Frames      | `Frame`     | e-ink text/bitmap frame model, scrolling           | [frames](frames.md) |
| Assets      | (free)      | PROGMEM bitmaps: battery, sleep, screensavers      | [assets](assets.md) |
| Config      | (free)      | pins, counters, prefs, SDK version stamping        | [configuration](configuration.md) |

The `frames` component is used by the OS's built-in apps. Third-party apps start from the simpler primitives in [oled](oled.md), [eink](eink.md), [font](font.md), and [layout](layout.md).

## Reading a component page

Every API page follows the same shape:

- **Access**: how the object is reached and what to include.
- **Lifecycle**: the `setup*()` call in `PocketMage_INIT()` that must run first, and how the component behaves across deep sleep and app load.
- **Reference**: one section per class or function. Signatures are verbatim from the headers; notes state runtime behavior, threading constraints, and return values.
- **Example**: a minimal snippet, usually drawing to one of the two displays.

Threading notes matter: the E-Ink panel is guarded by a recursive mutex and refreshed from a background task; code running in other tasks must take the
