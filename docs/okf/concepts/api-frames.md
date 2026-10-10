---
type: api
title: "Frames (frames.h)"
description: "The Frame data model and the e-ink text/bitmap rendering engine."
source: "https://talismandesign.github.io/PocketMage_SDK/docs/api/frames/"
path: /api/frames/
updated: 2026-10-10
okf:
  generated_by: "@docmd/plugin-okf"
  generated_at: "2026-10-10T16:00:43.160Z"
---
---
title: "Frames (frames.h)"
description: "The Frame data model and the e-ink text/bitmap rendering engine."
---

# Frames API

The frames module is the E-Ink text engine used by the OS's built-in apps. A `Frame` describes a screen region and its content (text or bitmap); `einkFramesDynamic` renders the frame stack with a scrollbar, cursor, and current-selection highlight.

## Content window model

```cpp
struct LineView {
  const char* ptr;   // NUL-terminated string in RAM or PROGMEM
  uint16_t    len;   // byte length (without the '\0')
  uint8_t     flags; // LineFlags
};

struct TextSource {               // read-only interface for any line list
  virtual ~TextSource() {}
  virtual size_t   size() const = 0;
  virtual LineView line(size_t i) const = 0;
};
```

`LineFlags` is `LF_NONE`, `LF_RIGHT`, `LF_CENTER`.

Two concrete sources:

```cpp
template<size_t MAX_LINES, size_t BUF_BYTES>
struct FixedArenaSource : TextSource {
  char buf[BUF_BYTES];
  uint16_t off[MAX_LINES];
  size_t nLines = 0, used = 0;
  void clear();
  bool pushLine(const char* s, uint16_t len, uint8_t flags = LF_NONE);
};

struct ProgmemTableSource : TextSource {
  const char* const* table;  // PROGMEM array of PROGMEM strings
  size_t count;
};
```

- `FixedArenaSource` owns its storage and copies pushed lines in; `pushLine` returns false when full (`MAX_LINES` or `BUF_BYTES`), so the caller can drop the oldest line.
- `ProgmemTableSource` wraps a PROGMEM table with no copy; lines are read character lengths with `strlen_P`.

The SDK declares the shared default sources:

```cpp
extern FixedArenaSource<512, 16384> frameLines;  // the live app window
extern ProgmemTableSource helpSrc;
extern const char* const HELP_LINES[] PROGMEM;   // plus HELP_COUNT and the
extern const size_t HELP_COUNT;                  // unit-conversion tables
```

The unit-conversion screen tables (length, area, volume, mass, temperature, energy, speed, pressure, data, angle, time, power, force, frequency) follow the `UNIT_TYPES_LINES`/`CONV_*_LINES` naming pattern, each with its `_COUNT`.

## The `Frame` layout

```cpp
class Frame {
public:
  enum class Kind : uint8_t { none, text, bitmap };
  int left, right, top, bottom;              // current geometry
  int origLeft, origRight, origTop, origBottom;
  int extendLeft, extendRight, extendTop, extendBottom;
  int bitmapW = 0, bitmapH = 0;
  bool cursor = false, box = false, invert = false, overlap = false;
  int choice = -1;                           // selected row index
  long scroll = 0, prevScroll = -1, lastTotal = -1;
  int maxLines = 0;
  Kind kind = Kind::none;
  const TextSource* source = nullptr;        // text frames
  const uint8_t* bitmap = nullptr;           // bitmap frames
  const uint8_t* font = nullptr;
  ...
  bool hasText()   const;
  bool hasBitmap() const;
};
```

Constructors: a plain region `Frame(l, r, t, b, cursor, box)`, a text frame `Frame(l, r, t, b, &source, cursor, box)`, and a bitmap frame `Frame(l, r, t, b, bitmapPtr, width, height, cursor, box)`.

Global frame state:

```cpp
extern Frame testBitmapScreen, testBitmapScreen1, testBitmapScreen2;
extern Frame testTextScreen;
extern Frame* CurrentFrameState;
extern int currentFrameChoice;
extern int frameSelection;
extern std::vector<Frame*> frames;
```

## Render and helpers

```cpp
void einkFramesDynamic(std::vector<Frame*>& frames, bool doFull_);
```

Renders every frame in the stack over the E-Ink buffer: readable-text padding (`kFrameTextPadX`, `kFrameCursorPad`), scrollbar from `updateScroll`, the current-selection highlight at `frame.choice`, and the cursor. The caller picks the screen refresh mode; the frame engine itself only fills the buffer.

```cpp
std::vector<String> formatText(Frame& frame, int maxTextWidth);
void drawLineInFrame(String& srcLine, int lineIndex, Frame& frame,
                     int usableY, bool clearLine);
void drawFrameBox(int usableX, int usableY, int usableWidth,
                  int usableHeight, bool invert);
int  computeCursorX(Frame& frame, bool rightAlign, bool centerAlign,
                    int16_t x1, uint16_t lineWidth);
std::vector<String> sourceToVector(const TextSource* src);
String frameChoiceString(const Frame& f);
void updateScroll(Frame* currentFrameState, int prevScroll,
                  int currentScroll, bool reset = false);
void updateScrollFromTouch_Frame();
void oledScrollFrame();
void getVisibleRange(Frame* f, long totalLines, long& startLine,
                     long& endLine);
```

`getVisibleRange` computes the slice of `totalLines` visible inside the frame given its scroll position, which is what callers iterate to draw windowed content.

## Example

```cpp
#include <pocketmage.h>

void runList() {
  frameLines.clear();
  frameLines.pushLine("line one");
  frameLines.pushLine("line two");
  Frame f(8, 312, 26, kEinkContentH, &frameLines, /*cursor=*/true,
          /*box=*/true);
  f.choice = 0;
  std::vector<Frame*> stack = {&f};
  einkFramesDynamic(stack, /*doFull=*/false);
  EINK().refresh();
}
