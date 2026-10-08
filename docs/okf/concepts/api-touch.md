---
type: api
title: "Touch (pocketmage_touch)"
description: "The MPR121 capacitive slider: scroll offsets, line scrolling, and gesture vectors."
source: "https://talismandesign.github.io/PocketMage_SDK/docs/api/touch/"
path: /api/touch/
updated: 2026-10-08
okf:
  generated_by: "@docmd/plugin-okf"
  generated_at: "2026-10-08T10:14:20.970Z"
---
---
title: "Touch (pocketmage_touch)"
description: "The MPR121 capacitive slider: scroll offsets, line scrolling, and gesture vectors."
---

# Touch API

`PocketmageTOUCH` reads the MPR121 capacitive slider and derives scroll gestures: a per-line dynamic offset for text apps, a step-wise `lineScroll` for list views, and a raw scroll vector for preview panels.

## Access

```cpp
#include <pocketmage.h>

PocketmageTOUCH& touch = TOUCH();
Adafruit_MPR121& cap = cap;   // raw driver (slider pads 0..8)
```

`TOUCH()` is the single instance. `setupTouch()` runs in `PocketMage_INIT()`. The slider reports the lowest-touched pad, which handles physical finger overlap; a "touch" is a pad index 0..8.

## Reference

### Window / dynamic scroll

```cpp
void updateScrollFromTouch();
long int getDynamicScroll() const;
void setDynamicScroll(long int val);
void setPrevDynamicScroll(long int val);
```

`updateScrollFromTouch()` maps a pad press to a page offset: sliding finger up (higher pad index) scrolls forward, down scrolls back, bounded by `allLines.size() - EINK().maxLines()`. The result is `getDynamicScroll()`, which `OLED().oledScroll()` uses for its window. Touches reset the auto sleep timer. Returns nothing; call it each loop iteration for the text pages.

### Step scrolling

```cpp
bool updateScroll(int maxScroll, ulong& lineScroll, int stepSize = 1);
```

Moves `lineScroll` by `stepSize` per pad step, clamped to `maxScroll`. Returns true when the view actually changed (so the caller redraws only then). `stepSize` makes fast-scroll modes possible.

### Gesture vector

```cpp
int getScrollVector();
int getDiff() const;
void setLastTouch(int val);
void setLastTouchTime(unsigned long val);
void resetLastTouch();
int getLastTouch() const;
unsigned long getLastTouchTime() const;
long int getPrevDynamicScroll() const;
```

`getScrollVector()` returns the signed movement of the most recent pad step (`lastTouchPos - touchPos`), 0 when idle or outside a one or two pad step. The remaining getters/setters expose the raw state (`lastTouch[Pos]`, `lastTouchPosTime`, `prev_dynamicScroll_`) for views that need to track gesture timing themselves. `getDiff()` is `dynamicScroll_ - prev_dynamicScroll_`.

## Timeout

After `TOUCH_TIMEOUT_MS` (1200 ms) with no touch, the gesture state resets so the next new touch is treated as the start of a fresh gesture (no stale step or vector). The reset also flags `newLineAdded` when the offset changed, so a preview can refresh once.

## Example

```cpp
#include <pocketmage.h>

void onLoop() {
  TOUCH().updateScrollFromTouch();
  // page view scrolls with TOUCH().getDynamicScroll();
}
