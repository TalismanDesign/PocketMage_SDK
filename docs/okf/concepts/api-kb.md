---
type: api
title: "Keyboard (pocketmage_kb)"
description: "The TCA8418 matrix keyboard and USB HID stack: modifiers, key events, and scanning."
source: "https://talismandesign.github.io/PocketMage_SDK/docs/api/kb/"
path: /api/kb/
updated: 2026-10-05
okf:
  generated_by: "@docmd/plugin-okf"
  generated_at: "2026-10-05T06:26:55.149Z"
---
---
title: "Keyboard (pocketmage_kb)"
description: "The TCA8418 matrix keyboard and USB HID stack: modifiers, key events, and scanning."
---

# Keyboard API

`PocketmageKB` wraps the TCA8418 key matrix plus the USB HID stack and turns button presses into characters, honoring the modifier state machine (NORMAL, SHIFT, FUNC, FN_SHIFT) and a cooldown filter.

## Access

```cpp
#include <pocketmage.h>

PocketmageKB& kb = KB();
Adafruit_TCA8418& pad = keypad;   // raw driver
```

`KB()` is the single instance. `setupKB(KB_IRQ)` runs in `PocketMage_INIT()` and wires `KB_IRQ` as the wake source. The OS stays responsive through the shared `PWR_BTN_event`/keypad interrupt flow; an app polls `updateKeypress()` from its main loop.

## Modifier state

```cpp
enum KBState { NORMAL, SHIFT, FUNC, FN_SHIFT };   // pocketmage_globals.h

void setKeyboardState(int kbState);
int  getKeyboardState() const;
void toggleShift();
void toggleFn();
```

`toggleShift` / `toggleFn` flip the modifier bits (FUNC toggles the Fn layer, and Shift within it). `getKeyboardState()` drives the OLED modifier badge in the status line. Layers compose: FN+Shift selects the `FN_SHIFT` layer for the secondary symbols.

## Key events

```cpp
char updateKeypress();
bool acceptKey();
void checkUSBKB();
```

- `updateKeypress()` returns the next character from either the USB HID FIFO (checked first) or the TCA8418 event FIFO, draining one key per call. It never blocks. The TCA8418 overflow register is cleared after each read so keys are not lost on a burst; the hardware FIFO is drained until empty before the `TCA8418_event_` flag clears.
- `acceptKey()` gating: returns true only if at least `KB_COOLDOWN` ms passed since the last accepted press. Use it before handling a key that must not auto-repeat.
- `checkUSBKB()` services the USB HID connection (call periodically).

The interrupt flag is exposed as a member (`TCA8418_event_`) and set by `setTCA8418Event()`; apps typically rely on the FIFO-drain behavior of `updateKeypress()` instead of the flag.

## Interrupt control

```cpp
void disableInterrupts();
void enableInterrupts();
void flush();
```

`disableInterrupts`/`enableInterrupts` gate the driver interrupts around long I/O; `flush()` drops any queued events (used at boot to clear stale presses, and after sleep).

## Example

```cpp
#include <pocketmage.h>

void pumpKeys() {
  char c = KB().updateKeypress();
  if (c == '\0') return;
  if (!KB().acceptKey()) return;
  // handle the character
}
