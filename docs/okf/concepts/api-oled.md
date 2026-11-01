---
type: api
title: "OLED (pocketmage_oled)"
description: "The 256x32 SSD1326 OLED: PocketmageOled class, the u8g2 object, and the status line."
source: "https://talismandesign.github.io/PocketMage_SDK/docs/api/oled/"
path: /api/oled/
updated: 2026-11-01
okf:
  generated_by: "@docmd/plugin-okf"
  generated_at: "2026-11-01T00:02:39.175Z"
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

A `bottomText` string is drawn centered in the status-line slot in place of the info bar. With `allowLarge`, a short word that fits the display width renders as the `Heading2` variant centered at `kOledWordLargeBaseline` (21); a word that does not fit falls through to the size cascade (`OledWord`, `Heading3`, `BodyBold`, `Caption`) centered at baseline 16.

## C ABI (`pm_*`)

External ELF apps call these C wrappers rather than the C++ class above; they are generated from it by `gen_app_api.py` and declared in `pm_sdk_app.h` (pulled in via `pm_app_api.h`). See the [function index](functions.md) and the [projection model](index.md#api-reference-the-c-abi-pm) for the general rules.

```c
void pm_oled_oled_word(const char* a0, bool a1, bool a2, const char* a3);
void pm_oled_oled_line(const char* a0, int a1, bool a2, const char* a3, bool a4);
void pm_oled_sysmsg(const char* a0, int a1);
void pm_oled_oled_scroll();
void pm_oled_info_bar();
void pm_oled_set_power_save(bool a0);
bool pm_oled_power_save();
```

All operate on the `OLED()` singleton.

- `pm_oled_oled_word(a0, a1, a2, a3)` is `oledWord(word, allowLarge, showInfo, bottomText)`; `a0` is the centered word, `a1` is `allowLarge`, `a2` is `showInfo`, `a3` is the optional `bottomText`.
- `pm_oled_oled_line(a0, a1, a2, a3, a4)` is `oledLine(line, input_pos, doProgressBar, bottomMsg, deferSend)`; `a0` is the editor line, `a1` is the cursor position, `a2` is `doProgressBar`, `a3` is the optional `bottomMsg`, `a4` is `deferSend`.
- `pm_oled_sysmsg(a0, a1)` is `sysMessage(msg, showTime)`; `a0` is the message, `a1` is the hold time in ms.
- `pm_oled_oled_scroll()` is `oledScroll()`.
- `pm_oled_info_bar()` is `infoBar()`.
- `pm_oled_set_power_save(a0)` is `setPowerSave(enable)`.
- `pm_oled_power_save()` is `getPowerSave()`. Returns the stored power-save flag.
