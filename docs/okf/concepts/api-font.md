---
type: api
title: "Fonts & Text (pocketmage_font)"
description: "FontStyle roles, DisplayTarget, the FontEngine static API, and the default font table."
source: "https://talismandesign.github.io/PocketMage_SDK/docs/api/font/"
path: /api/font/
updated: 2026-10-09
okf:
  generated_by: "@docmd/plugin-okf"
  generated_at: "2026-10-09T11:27:10.771Z"
---
---
title: "Fonts & Text (pocketmage_font)"
description: "FontStyle roles, DisplayTarget, the FontEngine static API, and the default font table."
---

# Font API

`FontEngine` is the single text path for both displays. Apps pick a logical `FontStyle` (a role, not a font file) and a `DisplayTarget` (OLED or E-Ink); the engine resolves the concrete u8g2 font, measures widths, caches metrics, and sets draw colors per target.

## Access

```cpp
#include <pocketmage.h>
```

`FontEngine` is a static-only class; there is no instance. The default table is `kDefaultFontTable`. `FontEngine::init()` runs inside `PocketMage_INIT()` and must precede any draw or measure call.

## Roles and targets

```cpp
enum class FontStyle : uint8_t {
  Tiny, Body, BodyBold, BodyItalic, BodyBoldItalic, Medium, Small,
  BodyNarrow, Mono, MonoBold, MonoItalic, MonoBoldItalic,
  Sans, SansBold, SansItalic, SansBoldItalic,
  Caption, Heading3, Heading2, Heading1, Large, OledWord,
  Terminal, TerminalBig, ClockDigit,
  _StyleCount   // sentinel, must be last
};

enum class DisplayTarget : uint8_t {
  OLED,   // u8g2 SSD1326
  EINK,   // u8g2f bridge on the GxEPD2 buffer
};
```

Each role maps to a differ fonte per target. The comment on each member in the header names the concrete font (for example `Body` is `ncenR10_tf`, `Heading1` is `ncenB24_tf`). `Tiny` is `u8g2_font_5x7_tf`, the size used for the OLED status line.

## Structs

### `FontEntry`

```cpp
struct FontEntry {
  const uint8_t* oled;   // u8g2 font for the OLED
  const uint8_t* eink;   // u8g2 font for the E-Ink
  uint8_t        height; // full cell height in pixels
};
```

### `FontTable`

A complete per-language/region table: one `FontEntry` per role plus the TXT markdown-editor matrix `txt[3][4][4]` indexed by family x size x variant. Applied with `FontEngine::init(&table)`; `nullptr` re-applies the built-in default.

The TXT matrix indices are named constants:

```cpp
enum : uint8_t {
  TxtFamilySerif = 0, TxtFamilySans = 1, TxtFamilyMono = 2,
  TxtSizeBody    = 0, TxtSizeH3    = 1, TxtSizeH2 = 2, TxtSizeH1 = 3,
  TxtVariantN    = 0, TxtVariantB  = 1, TxtVariantI = 2, TxtVariantBI = 3,
};
```

Heading regular variants alias to their bold font; headings are always bold. `kDefaultFontTable` is the English/global table.

## `FontEngine` reference

```cpp
class FontEngine {
public:
  static void init(const FontTable* table = nullptr);

  static void drawText(DisplayTarget target, int x, int y,
                       const char* text, FontStyle style);
  static void drawText(DisplayTarget target, int x, int y,
                       const String& text, FontStyle style);
  static void drawGlyph(DisplayTarget target, int x, int y,
                        uint16_t unicode, FontStyle style);

  static int textWidth(DisplayTarget target, const char* text, FontStyle style);
  static int textWidth(DisplayTarget target, const String& text, FontStyle style);
  static FontStyle fitStyle(DisplayTarget target, const char* text, int maxWidth,
                            const FontStyle* cascade, int count);
  static int charWidth(DisplayTarget target, uint16_t unicode, FontStyle style);
  static int fontHeight(DisplayTarget target, FontStyle style);
  static int fontAscent(DisplayTarget target, FontStyle style);
  static int fontDescent(DisplayTarget target, FontStyle style);
  static void setTextColor(DisplayTarget target, uint16_t color);

  static void drawTextTxt(DisplayTarget target, int x, int y, const char* text,
                          uint8_t family, uint8_t sizeIdx, uint8_t variant);
  static void drawGlyphTxt(DisplayTarget target, int x, int y, uint16_t unicode,
                           uint8_t family, uint8_t sizeIdx, uint8_t variant);
  static int textWidthTxt(DisplayTarget target, const char* text,
                          uint8_t family, uint8_t sizeIdx, uint8_t variant);
  static int charWidthTxt(DisplayTarget target, uint16_t unicode,
                          uint8_t family, uint8_t sizeIdx, uint8_t variant);
  static int fontHeightTxt(uint8_t family, uint8_t sizeIdx, uint8_t variant);
};
```

### Drawing

- `drawText` / `drawGlyph` render UTF-8 text or a single codepoint at `(x, y)`; `y` is the baseline, not the top (U8g2 convention).
- Out-of-range `family`/`sizeIdx`/`variant` in the `Txt` variants clamp to the body entry rather than fault.

### Measuring

- `textWidth` measures without changing the currently applied font, so layout code can interleave measure/draw freely.
- `charWidth` uses a per-style cache for codepoints 32 through 255 on the OLED target; other codepoints and the E-Ink target are measured live.
- `fontHeight` / `fontAscent` / `fontDescent` are measured lazily on first per (target, style) call and cached; layout code never switches fonts merely to measure.

### `fitStyle`

The width-aware selection used by the HOME/APPLOADER grid to keep localized app names inside the 60px cell pitch: picks the largest cascade entry (ordered largest first) whose `textWidth` fits `maxWidth`, or the smallest when none fit. Typical cascade: `kLabelCascade` (`Body`, `BodyNarrow`, `Small`).

### `setTextColor`

Value semantics match u8g2 draw color: 0 = black, 1 = white. Only the requested target's color is touched, so an E-Ink status-bar draw never changes the OLED draw color and vice versa. Set the E-Ink color before the first draw of a screen; the bridge defaults are black-on-white from `setupEink()`.

## Example

```cpp
#include <pocketmage.h>

void drawTitle() {
  FontEngine::setTextColor(DisplayTarget::EINK, GxEPD_BLACK);
  FontEngine::drawText(DisplayTarget::EINK, 8, 60, "PocketMage",
                       FontStyle::Heading1);

  int w = FontEngine::textWidth(DisplayTarget::OLED, "Files", FontStyle::Body);
  // center on the OLED
  FontEngine::drawText(DisplayTarget::OLED,
                       (kOledWidth - w) / 2, kOledWordBaseline,
                       "Files", FontStyle::Body);
}
```

Remember `y` is the baseline. `kOledWordBaseline` (16) and the
