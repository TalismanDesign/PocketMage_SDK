---
type: api
title: "E-Ink (pocketmage_eink)"
description: "The 320x240 GDEQ031T10 panel: PocketmageEink, the background refresh task, and refresh policy."
source: "https://talismandesign.github.io/PocketMage_SDK/docs/api/eink/"
path: /api/eink/
updated: 2026-09-30
okf:
  generated_by: "@docmd/plugin-okf"
  generated_at: "2026-09-30T00:52:21.248Z"
---
---
title: "E-Ink (pocketmage_eink)"
description: "The 320x240 GDEQ031T10 panel: PocketmageEink, the background refresh task, and refresh policy."
---

# E-Ink API

The 320x240 GDEQ031T10 e-ink panel is the full-page working display. Text rendering goes through `FontEngine` via the `u8g2f` bridge; `PocketmageEink` owns the panel driver, the refresh policy (fast partial vs slow full), the status bar, and a background refresh task.

## Access

```cpp
#include <pocketmage.h>

PocketmageEink& eink = EINK();       // singleton
DisplayT& einkDisplay = display;     // GxEPD2 BW driver
U8G2_FOR_ADAFRUIT_GFX &tf = u8g2f;   // text bridge for the panel
```

`EINK()` returns the single instance. `setupEink()` runs in `PocketMage_INIT()`: it initializes the panel at 16 MHz SPI, sets rotation 3, starts `einkHandlerTask` on core 0 (10 KB stack, priority 1), and registers the `u8g2f` bridge with black on white defaults.

## Refresh policy

All panel drawing goes through `refresh()`, which owns the update rhythm:

- **`FAST_REFRESH` off (production default).** Every `refresh()` is a full update with `display()`; a fast waveform is used for normal redraws and a slow ~3s waveform for the periodic clean, tracked by `FULL_REFRESH_AFTER`. The panel is hibernated afterwards.
- **`FAST_REFRESH` on (beta default).** Differential partial updates (~0.65s) with a full update at boot, after wake, and every `FAST_REFRESH_AFTER` partials; a slow clean removes accumulated ghosting. `forceSlowFullUpdate(true)` forces the next refresh to take the slow full path regardless of counters.

`markPanelNeedsFullRefresh()` forces a real full update on the next refresh; the OS calls it at wake so the panel resyncs with GxEPD2's previous-image RAM.

## Class reference

### `PocketmageEink`

```cpp
class PocketmageEink {
public:
  void setLineSpacing(uint8_t lineSpacing);
  void setFullRefreshAfter(uint8_t fullRefreshAfter);
  void refresh();
  void setFastFullRefresh(bool setting);
  void statusBar(const String& input, bool fullWindow = false);
  void drawStatusBar(const String& input);
  void resetDisplay(bool clearScreen = true, uint16_t color = GxEPD_WHITE);
  int  countLines(const String& input, size_t maxLineLength = 29);
  uint8_t getFontHeight();
  int maxLines();
  uint16_t getEinkTextWidth(const String& s);
  uint8_t getLineSpacing();
  DisplayT& getDisplay();
  void forceSlowFullUpdate(bool force);
  void markPanelNeedsFullRefresh();
  void lockPanel();
  void unlockPanel();
};
```

#### `refresh`

Renders the current buffer per the refresh policy above. It takes the panel mutex for the whole redraw; another task already holding the panel blocks until it finishes. Clears the framebuffer and powers the panel off (or hibernates in the legacy path) afterwards, so the next screen build starts from white.

#### `statusBar` / `drawStatusBar`

Draws the 26px status band: a bounded 20px box with the text in the white region above it, a battery box on the right (30px), and the input text fitted with `kLabelCascade` + ellipsis. `drawStatusBar` does the band; `statusBar(true)` forces a partial-window draw first so only the band area refreshes when the caller relies on `display()` afterward.

#### `resetDisplay`

Sets rotation 3 and the full window, optionally filling `color`. Call before rebuilding a screen when the prior frame is unknown.

#### Layout helpers

- `countLines(input, maxLineLength)` counts rendered lines for an unwrapped string (wraps at `\n` and at `maxLineLength`).
- `maxLines()` returns how many body lines fit the content region.
- `getFontHeight()` and `getEinkTextWidth()` measure with `FontStyle::Body` (see [font](font.md)).

#### `lockPanel` / `unlockPanel`

The recursive mutex. `refresh()` takes it itself; hold it when a task draws into the framebuffer directly, so a background refresh cannot swap buffers mid-draw. See [threading](index.md#reading-a-component-page).

## The background task

```cpp
void einkHandler(void *parameter);
```

Created by `setupEink()` on core 0: after a 250 ms settle it calls the app hook `einkHandler_APP()` (app builds) or `applicationEinkHandler()` (host builds, see [`pocketmage_globals.h`](https://github.com/TalismanDesign/PocketMage_SDK/blob/main/pocketmage_globals.h)) every 50 ms, then yields. The hook is where an app does its periodic redraws. On deep sleep the task is deleted under the panel lock so an in-flight refresh completes first.

## Example

```cpp
#include <pocketmage.h>

extern "C" int main(int, char**) {
  FontEngine::setTextColor(DisplayTarget::EINK, GxEPD_BLACK);
  FontEngine::drawText(DisplayTarget::EINK, 8, 120, "hello eink",
                       FontStyle::Heading1);
  EINK().statusBar("pocketmage");
  EINK().refresh();
  return 0;
}
```

The buffer is the GLCD framebuffer of `display`; fill it with
