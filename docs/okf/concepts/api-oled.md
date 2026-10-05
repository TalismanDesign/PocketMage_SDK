---
type: api
title: "OLED (pocketmage_oled)"
description: "The 256x32 SSD1326 OLED: PocketmageOled class, the u8g2 object, and the status line."
source: "https://talismandesign.github.io/PocketMage_SDK/docs/api/oled/"
path: /api/oled/
updated: 2026-10-05
okf:
  generated_by: "@docmd/plugin-okf"
  generated_at: "2026-10-05T06:26:55.150Z"
---
---
title: "OLED (pocketmage_oled)"
description: "The 256x32 SSD1326 OLED: PocketmageOled class, the u8g2 object, and the status line."
---

# OLED API

The 256x32 SSD1326 SPI OLED shows the short text line, the status bar, and the battery/clock info during normal interaction. Text on it goes through `FontEngine` (see [font](font.md)); the `PocketmageOled` methods handle the whole-buffer draws, the info bar, and power save.

## Access

```cpp
#include <pocketmage.h>

PocketmageOled& oled = OLED();   // singleton
U8G2_SSD1326_ER_256X32_F_4W_HW_SPI &u8g2u = u8g2;  // raw driver, if needed
```

`OLED()` returns the single instance held by the SDK. `u8g2` is the raw U8G2 driver object; draw directly to it only when a `PocketmageOled` method does not cover the primitive you need. `setupOled()` runs inside `PocketMage_INIT()` before anything uses the display.

## Class reference

### `PocketmageOled`

```cpp
class PocketmageOled {
public:
  explicit PocketmageOled(U8G2 &u);

  void oledWord(String word, bool allowLarge = false,
                bool showInfo = true, String bottomText = "");
  void oledLine(String line, int input_pos, bool doProgressBar = true,
                String bottomMsg = "", bool deferSend = false);
  void sysMessage(String msg, int showTime = 1500);
  void oledScroll();
  void infoBar();
  void setPowerSave(bool enable);
  bool getPowerSave() const;
};
```

#### `oledWord`

Centers `word` on the display. Font size cascades down (OledWord then Heading3, BodyBold, Caption) until the text fits 256px. With `allowLarge`, an 18pt `Heading2` first attempt is used when it fits. Unless `showInfo` is false with no `bottomText`, the status line is drawn first. A `bottomText` replaces the status line. Sends the buffer.

#### `oledLine`

Renders one editor line at the edit baseline with a block cursor at `input_pos`, a top progress bar showing how far the line is past the reference width, and the info bar (or `bottomMsg`). Use it for text-input screens. `deferSend` leaves the buffer pending so the caller draws extra primitives and sends once.

#### `sysMessage`

Raises a framed message from the bottom of the display, holds it for `showTime` ms, and lowers it. Temporarily bumps the CPU to 240 MHz for the animation and restores `POWER_SAVE_FREQ` when `SAVE_POWER` is set. Use it for transient confirmations and errors.

#### `oledScroll`

The line-scroll preview view for text apps: a left bar strip proportional to line lengths, a `n/total` line counter, and the current line preview. Reads `allLines` and `TOUCH().getDynamicScroll()` (see [touch](touch.md) and [sys](sys.md)).

#### `infoBar`

The OLED status line: keyboard modifier, battery bitmap indexed by `battState`, clock and date (when `SYSTEM_CLOCK`), and the MSC, sink, and SD indicators. Called internally by `oledWord` and `oledLine`.

#### `setPowerSave`

Toggles the driver power save and stores the flag. `getPowerSave()` returns the stored state.

## Status line

The bottom row (baseline `kOledInfoBaseline`, 26px tall) is the status line. Battery uses the `batt_allArray` bitmaps indexed by the shared `battState` global; the clock cell uses `I18n::dayName()` and the active month/day/year. `SYSTEM_CLOCK` and `SHOW_YEAR` are the persistent toggles from [configuration](configuration.md).

## Threading

The OLED driver is called directly; there is no ownership lock. Keep OLED draws to the task that owns the screen (the app's main flow or the keyboard task) and never interleave with an [eink](eink.md) redraw on the same SPI bus without coordination. Deep sleep power-saves the OLED first (see [sys](sys.md)).

## Example

```cpp
#include <pocketmage.h>

void showStatus() {
  OLED().oledWord("Files", /*allowLarge=*/true);
  OLED().sysMessage("Saved", 1200);
  OLED().setPowerSave(false);
}
```

`bottomText` display string is drawn centered in the status-line slot; `allowLarge` with a short word renders the `Heading2` variant at the large
