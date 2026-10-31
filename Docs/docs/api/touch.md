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
```

## C ABI (`pm_*`)

External ELF apps call these C wrappers rather than the C++ class above; they are generated from it by `gen_app_api.py` and declared in `pm_sdk_app.h` (pulled in via `pm_app_api.h`). See the [function index](functions.md) and the [projection model](index.md#api-reference-the-c-abi-pm) for the general rules.

```c
void pm_touch_update_scroll_from_touch();
bool pm_touch_update_scroll(int a0, unsigned long* a1, int a2);
int pm_touch_get_scroll_vector();
long pm_touch_get_dynamic_scroll();
long pm_touch_get_prev_dynamic_scroll();
int pm_touch_get_last_touch();
unsigned long pm_touch_get_last_touch_time();
int pm_touch_get_diff();
```

All operate on the `TOUCH()` singleton.

- `pm_touch_update_scroll_from_touch()` is `updateScrollFromTouch()`.
- `pm_touch_update_scroll(a0, a1, a2)` is `updateScroll(maxScroll, lineScroll, stepSize)`; `a0` is `maxScroll`, `a2` is `stepSize`. The `ulong&` reference parameter becomes the out-pointer `a1`: the current `lineScroll` is read from and written back through it. Returns true when the view changed.
- `pm_touch_get_scroll_vector()` is `getScrollVector()`. Returns the signed movement of the most recent pad step, 0 when idle.
- `pm_touch_get_dynamic_scroll()` is `getDynamicScroll()`.
- `pm_touch_get_prev_dynamic_scroll()` is `getPrevDynamicScroll()`.
- `pm_touch_get_last_touch()` is `getLastTouch()`.
- `pm_touch_get_last_touch_time()` is `getLastTouchTime()`.
- `pm_touch_get_diff()` is `getDiff()`; the `dynamicScroll_ - prev_dynamicScroll_` difference.
