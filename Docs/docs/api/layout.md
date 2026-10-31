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
```

## C ABI (`pm_*`)

External ELF apps call these C wrappers rather than the C++ functions above; they are generated from them by `gen_app_api.py` and declared in `pm_sdk_app.h` (pulled in via `pm_app_api.h`). See the [function index](functions.md) and the [projection model](index.md#api-reference-the-c-abi-pm) for the general rules.

```c
int pm_layout_eink_row_pitch(int a0);
size_t pm_layout_slice_that_fits(const char* a0, size_t a1, int a2, int a3);
const char* pm_layout_truncate_with_ellipsis(const char* a0, int a1, int a2, int a3);
int pm_layout_word_wrap_count(const char* a0, int a1, int a2);
int pm_layout_word_wrap_get(const char* a0, int a1, int a2, int index, char* out, size_t out_size);
```

- `pm_layout_eink_row_pitch(a0)` is `einkRowPitch(s)`; `a0` is a `pm_style` value as int. Returns the canonical row height for that style.
- `pm_layout_slice_that_fits(a0, a1, a2, a3)` is `sliceThatFits(s, n, maxTextWidth, style)`; `a0` is the string, `a1` is the byte length, `a2` is the pixel budget, `a3` is a `pm_style`. Returns the byte length of the longest prefix that fits, or 0 when nothing fits.
- `pm_layout_truncate_with_ellipsis(a0, a1, a2, a3)` is `truncateWithEllipsis(text, maxWidthPx, style, target)`; `a0` is the text, `a1` is the pixel budget, `a2` is a `pm_style`, `a3` is a `pm_target` value as int. Returns a pointer into the rotating string pool.
- `pm_layout_word_wrap_count` / `pm_layout_word_wrap_get` project `wordWrap()`, which returns `std::vector<String>`. Call `pm_layout_word_wrap_count(a0, a1, a2)` for the line count, then `pm_layout_word_wrap_get(a0, a1, a2, index, out, out_size)` per index to copy a wrapped line into the caller buffer `out` (at most `out_size` bytes). `a0` is the text, `a1` is the pixel budget, `a2` is a `pm_style`. `_get` returns the line length or -1 for a bad index. These write into the caller buffer, not the rotating pool.
