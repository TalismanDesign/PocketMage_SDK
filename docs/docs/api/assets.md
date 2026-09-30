---
title: "Built-in asset bitmaps (libAssets.h)"
description: "Compiled-in PROGMEM bitmaps for the status bar, screensavers, and battery gauge."
---

# Built-in assets

The SDK ships small PROGMEM bitmaps for the OS status line and screensavers. Apps can draw them directly with the E-Ink/OLED bitmap primitives, or they can draw custom assets from the SD card (`/assets/...`, see [publishing](../publish.md)) instead.

```cpp
#include <pocketmage.h>
// assets are visible from the umbrella via libAssets.h
```

## Battery gauge

Six frames, indexed 0 (empty) to 5 (full):

```cpp
extern const unsigned char _batt0 [] PROGMEM;
extern const unsigned char _batt1 [] PROGMEM;
extern const unsigned char _batt2 [] PROGMEM;
extern const unsigned char _batt3 [] PROGMEM;
extern const unsigned char _batt4 [] PROGMEM;
extern const unsigned char _batt5 [] PROGMEM;
extern const unsigned char* batt_allArray[6];
```

The OLED status line selects `batt_allArray[battState]`, where `battState` comes from `getBatteryVoltage()` ([sys](sys.md)). Frame dimensions follow `kOledInfoBatteryW`/`kOledInfoBatteryH` ([layout](layout.md)).

## Scroll indicator

```cpp
extern const unsigned char scrolloled0 [] PROGMEM;
```

## Sleep icons

```cpp
extern const unsigned char sleep0 [] PROGMEM;
extern const unsigned char sleep1 [] PROGMEM;
```

## Screensavers

```cpp
extern const unsigned char _ScreenSaver0 [] PROGMEM;
extern const unsigned char _ScreenSaver1 [] PROGMEM;
// ... through
extern const unsigned char _ScreenSaver17 [] PROGMEM;
extern const unsigned char* ScreenSaver_allArray[18];
```

18 built-in 320x240 1-bit screensavers. `deepSleep()` picks one at random at rotation 3, unless `/assets/backgrounds/*.bin` files exist on the card (in which case a card background wins).

## Not implemented

The `KBStatus*` entries are declared but not populated; they are not used by the OS and their license is pending. Avoid them.

## Example

```cpp
#include <pocketmage.h>
#include <pocketmage_assets/libAssets.h>   // included by pocketmage.h

void drawOwnBatteryGauge(int x, int y) {
  FontEngine::setTextColor(DisplayTarget::EINK, GxEPD_BLACK);
  EINK().drawBitmap(x, y, batt_allArray[battState], 10, 6, GxEPD_BLACK);
}
