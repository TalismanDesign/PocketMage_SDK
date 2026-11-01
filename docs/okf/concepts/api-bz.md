---
type: api
title: "Buzzer (pocketmage_bz)"
description: "PWM buzzer output, Note/Jingle data model, and the built-in melodies."
source: "https://talismandesign.github.io/PocketMage_SDK/docs/api/bz/"
path: /api/bz/
updated: 2026-11-01
okf:
  generated_by: "@docmd/plugin-okf"
  generated_at: "2026-11-01T01:09:38.799Z"
---
---
title: "Buzzer (pocketmage_bz)"
description: "PWM buzzer output, Note/Jingle data model, and the built-in melodies."
---

# Buzzer API

`PocketmageBZ` drives the PWM buzzer (10-bit LEDC channel 1 on `BZ_PIN`) and plays note sequences ("jingles"). Melodies are compile-time `Jingle` values: `constexpr` note arrays plus a length, so a tune costs no RAM at runtime.

## Access

```cpp
#include <pocketmage.h>

PocketmageBZ& bz = BZ();
```

`BZ()` is the single instance. `setupBZ()` runs in `PocketMage_INIT()` and calls `begin()`; the startup jingle plays automatically.

## Data model

```cpp
struct Note {
  int key;      // MIDI-ish note number (NOTE_* constants)
  int duration; // arbitrary ticks (see NOTE_* in the header)
};

struct Jingle {
  const Note* notes;  // PROGMEM-friendly constant array
  size_t len;
};

namespace Jingles {
  extern const Jingle Startup;    // ascending arpeggio
  extern const Jingle Shutdown;   // descending arpeggio
}
```

Built-in melodies use `NOTE_A8` / `NOTE_B8` / `NOTE_C8` / `NOTE_D8` constants from the include set. A custom jingle is a new `constexpr` note array plus a `Jingle`:

```cpp
constexpr static const Note myNotes[] = {
    {NOTE_A8, 120}, {NOTE_B8, 120}, {NOTE_C8, 120}, {NOTE_D8, 120}};
const Jingle myMelody = {myNotes, sizeof(myNotes) / sizeof(myNotes[0])};
```

## Class reference

```cpp
class PocketmageBZ {
public:
  PocketmageBZ();
  bool begin(int channel = 0);
  void end();
  void playJingle(const Jingle& jingle);
};
```

- `begin(channel)` attaches the buzzer to the PWM channel and is idempotent in `setupBZ()`.
- `playJingle(jingle)` plays the sequence synchronously (back-to-back notes) and re-attaches the channel if it was torn down. Use `MUTE_BUZZER` to silence without removing calls.

`PWM_CHANNEL` is `LEDC_CHANNEL_1`, `PWM_RESOLUTION` is `LEDC_TIMER_10_BIT`.

## Example

```cpp
#include <pocketmage.h>

void announce() {
  BZ().playJingle(Jingles::Startup);
}
```

## C ABI (`pm_*`)

External ELF apps call these C wrappers rather than the C++ class above; they are generated from it by `gen_app_api.py` and declared in `pm_sdk_app.h` (pulled in via `pm_app_api.h`). See the [function index](functions.md) and the [projection model](index.md#api-reference-the-c-abi-pm) for the general rules.

```c
bool pm_bz_begin(int a0);
void pm_bz_end();
void pm_bz_play_jingle(int a0);
```

- `pm_bz_begin(a0)` is `PocketmageBZ::begin(channel)` on `BZ()`; `a0` is the PWM channel. Returns true on success.
- `pm_bz_end()` is `end()`.
- `pm_bz_play_jingle(a0)` is `playJingle()`; `a0` is a `pm_jingle` value as int (`PM_JINGLE_STARTUP` or `PM_JINGLE_SHUTDOWN`) selecting a built-in melody. Plays synchronously.
