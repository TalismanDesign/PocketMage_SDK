---
type: api
title: "Layout (pocketmage_layout)"
description: "Canonical display geometry, the row-pitch helper, and the text-fit free functions."
source: "https://talismandesign.github.io/PocketMage_SDK/docs/api/layout/"
path: /api/layout/
updated: 2026-10-05
okf:
  generated_by: "@docmd/plugin-okf"
  generated_at: "2026-10-05T01:48:20.782Z"
---
---
title: "Layout (pocketmage_layout)"
description: "Canonical display geometry, the row-pitch helper, and the text-fit free functions."
---

# Layout API

The layout module centralizes display geometry and the text-fitting helpers used by the OS and the display primitives. It commits to one hardware pair: a 256x32 SSD1326 OLED and a 320x240 GDEQ031T10 e-ink panel.

## Geometry constants

```cpp
constexpr int kOledWidth  = 256;
constexpr int kOledHeight = 32;
constexpr int kEinkWidth  = 320;
constexpr int kEinkHeight = 240;

constexpr int kEinkStatusH  = 26;                 // bottom status band
constexpr int kEinkContentH = kEinkHeight - kEinkStatusH;  // 214
```

The e-ink content region is `kEinkHeight - kEinkStatusH` tall; the status band is owned by `EINK().drawStatusBar()`.

## Text frames

```cpp
constexpr int kFrameTextPadX  = 4;   // left text pad inside an e-ink frame
constexpr int kFrameCursorPad = 16;  // right/center clearance for the cursor
```

## OLED baselines and the info line

```cpp
constexpr int kOledWordBaseline      = 16;  // oledWord/sysMessage center
constexpr int kOledWordLargeBaseline = 21;  // oledWord allowLarge heading
constexpr int kOledEditBaseline      = 20;  // oledLine input/cursor
constexpr int kOledEditRightPad      = 8;
constexpr int kOledEditCursorX       = 0;
constexpr int kOledEditCursorY       = 1;
constexpr int kOledEditCursorH       = 22;
constexpr int kOledInfoBaseline      = kOledHeight;       // status line
constexpr int kOledInfoBatteryW      = 10;
constexpr int kOledInfoBatteryH      = 6;
constexpr int kOledInfoBatteryY      = kOledHeight - kOledInfoBatteryH;
constexpr int kOledInfoGap           = 6;
constexpr int kOledInfoFirstX        = kOledInfoBatteryW + kOledInfoGap;
```

Progress bar and sysMessage overlay geometry:

```cpp
constexpr int kOledProgressMaxW  = kEinkWidth - 5;  // reference width
constexpr float kOledProgressFullFrac = 0.8f;       // "full" arrow threshold
constexpr int kOledSysMsgPadX   = 8;
constexpr int kOledSysMsgFrameH = kOledHeight + 16;
constexpr int kOledSysMsgRadius = 10;
constexpr int kOledSysMsgRaise  = 5;
```

## Scroll preview

```cpp
constexpr int kOledScrollPreviewW = 128;   // preview strip width
constexpr int kOledScrollRowPitch = 4;     // bar vertical pitch
constexpr int kOledScrollBarH     = 2;
constexpr int kOledScrollBaseY    = 28;
constexpr int kOledScrollNormX    = 61;
constexpr int kOledScrollTabX     = 68;
constexpr int kOledScrollNormMaxW = 56;
constexpr int kOledScrollTabMaxW  = 49;
constexpr int kOledScrollTextX    = 140;
constexpr int kOledScrollLabelY   = 12;
constexpr int kOledScrollValueY   = 24;
```

## Icon grid and label cascade

```cpp
constexpr int kIconCellSize = 40;   // app icon cell
constexpr int kIconNameGap  = 13;   // icon-to-name baseline gap
constexpr int kGridLabelMaxW = 60;  // max label width = cell pitch

static constexpr FontStyle kLabelCascade[] = {
  FontStyle::Body, FontStyle::BodyNarrow, FontStyle::Small,
};
```

`kLabelCascade` is the standard fit cascade (all serif: `ncenR10`, `timR10`, `ncenR08`) used by the HOME/APPLOADER grid and the SETTINGS list. `einkRowPitch()` converts a style to a canonical row height:

```cpp
inline int einkRowPitch(FontStyle s) {
  return FontEngine::fontHeight(DisplayTarget::EINK, s) + EINK().getLineSpacing();
}
```

## Free functions

### `sliceThatFits`

```cpp
size_t sliceThatFits(const char* s, size_t n, int maxTextWidth, FontStyle style);
```

Returns the byte length of the longest prefix of `s[0..n)` that fits in `maxTextWidth` at `style`, word-aware: a trailing space boundary is preferred, so a caller can slice line by line without mid-word cuts. Stops at a newline (returns the fit before it, or 1 for a bare newline). Returns 0 when nothing fits.

### `truncateWithEllipsis`

```cpp
String truncateWithEllipsis(const String& text, int maxWidthPx, FontStyle style,
                            DisplayTarget target = DisplayTarget::EINK);
```

Truncates `text` to `maxWidthPx` and appends `"..."` (binary-search cut, so it is fast even on long strings). The cut lands on a UTF-8 character boundary: continuation bytes are backed off until the prefix ends cleanly. Returns `text` unchanged when it already fits.

### `wordWrap`

```cpp
std::vector<String> wordWrap(const String& text, int maxWidthPx, FontStyle style);
```

Wraps to whole words, one string per line, using `sliceThatFits`. Lines are trimmed; an overlong word pushes as a single (n) line.

## Example

```cpp
#include <pocketmage.h>

void drawBudget(std::vector<String>& lines) {
  int y = einkRowPitch(FontStyle::Body);
  for (const String& line : lines) {
    FontEngine::drawText(DisplayTarget::EINK, kFrameTextPadX, y, line,
                         FontStyle::Body);
    y += einkRowPitch(FontStyle::Body);
    if (y > kEinkContentH) break;
  }
}
