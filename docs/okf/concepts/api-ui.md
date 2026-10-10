---
type: api
title: "UI helpers (pocketmage_ui)"
description: "Screen scaffolding, scrollbars, list rows, inverted chips, and the OLED cycle picker."
source: "https://talismandesign.github.io/PocketMage_SDK/docs/api/ui/"
path: /api/ui/
updated: 2026-10-10
okf:
  generated_by: "@docmd/plugin-okf"
  generated_at: "2026-10-10T16:00:43.164Z"
---
---
title: "UI helpers (pocketmage_ui)"
description: "Screen scaffolding, scrollbars, list rows, inverted chips, and the OLED cycle picker."
---

# UI Helpers

`pocketmage_ui` are the shared full-screen and list primitives that the OS's built-in apps build on, and that third-party apps can reuse directly. They draw into the E-Ink buffer, so pair them with a single `EINK().refresh()` (or `endEinkScreen`).

## Access

```cpp
#include <pocketmage.h>
```

All functions are free functions; there is no object. `DisplayT` is the e-ink driver (`display`) and `U8G2` is the OLED driver (`u8g2`).

## Reference

### Screen lifecycle

```cpp
enum class EinkRefresh : uint8_t { Normal, ForceFull };

void beginEinkScreen(bool preserveBg = false);
void endEinkScreen(const char* statusText, EinkRefresh mode = EinkRefresh::Normal);
```

- `beginEinkScreen(preserveBg)` calls `EINK().resetDisplay(!preserveBg)`: clear the buffer (or keep the previous background) and set the full window.
- `endEinkScreen(statusText, mode)` draws the status band from `statusText` via `EINK().drawStatusBar`, then refreshes. `ForceFull` forces the slow full update before `refresh()` (for content changes that must not ghost).

### `drawScrollbar`

```cpp
void drawScrollbar(int total, int visible, int index,
                   int barX = -1, int barY = 0, int barH = -1,
                   int barW = 3, bool clearBg = false,
                   uint16_t fg = GxEPD_BLACK, uint16_t bg = GxEPD_WHITE);
```

Right-edge scrollbar. `barX`/`barH` of -1 default to the right edge and the full height. The handle is at least 15px tall and proportional to `visible/total`; `index` is clamped to the scroll range. `clearBg` erases the previous handle area first (the caller typically fills it with `bg`).

### `drawListItem`

```cpp
void drawListItem(int x, int y, const String& text, int maxWidth = -1);
```

Draws a `Body` list row at `(x, y)`. With `maxWidth > 0` the text is truncated with an ellipsis to fit.

### `drawChipText`

```cpp
int drawChipText(int x, int baselineY, const String& text,
                 FontStyle style, int maxTextW,
                 bool inverted, int chipMaxW = -1,
                 int padX = 8, int chipH = 20, int bottomPad = 6);
```

Draws an inverted "tag" chip behind e-ink text (selected list rows, key badges). When `maxTextW > 0`, `style` is first re-selected with `fitStyle` against `kLabelCascade` and the text is truncated to fit. The chip rect is `inkW + 2*padX` wide and `chipH` tall, topped at `baselineY + bottomPad - chipH` with 4px corners; filling it does not disturb the text color for later draws (the bridge foreground is restored after an inverted chip). Returns the chip width in pixels.

### `drawCyclePickerOLED`

```cpp
void drawCyclePickerOLED(U8G2& u8g2, const char* const* items, int count,
                         int selected, const char* badge = nullptr);
```

The OLED cycle picker: a centered panel of `count` cells with the selected item filled, and an optional `badge` chip below the panel's bottom edge. Cells shrink (`kMaxCellW = 22`) so long lists never overflow 256px. Draws the full buffer contents; the caller sends the buffer.

## Example

```cpp
#include <pocketmage.h>

extern "C" int main(int, char**) {
  beginEinkScreen();
  drawListItem(8, 60, "WiFi", 200);
  int w = drawChipText(200, 60, "On", FontStyle::Body, 60, true);
  drawScrollbar(40, 6, 12);
  endEinkScreen("Settings");
  return 0;
}
