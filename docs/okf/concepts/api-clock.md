---
type: api
title: "Clock (pocketmage_clock)"
description: "The PCF8563 RTC: reading, setting, validity, and the auto-sleep timer bookkeeping."
source: "https://talismandesign.github.io/PocketMage_SDK/docs/api/clock/"
path: /api/clock/
updated: 2026-10-10
okf:
  generated_by: "@docmd/plugin-okf"
  generated_at: "2026-10-10T16:00:43.158Z"
---
---
title: "Clock (pocketmage_clock)"
description: "The PCF8563 RTC: reading, setting, validity, and the auto-sleep timer bookkeeping."
---

# Clock API

`PocketmageCLOCK` wraps the PCF8563 real-time clock on the primary I2C bus. It answers "what time is it," sets the time from a user string, validates the chip, and tracks the millis-based auto-sleep timeout that the OS feeds from idle activity.

## Access

```cpp
#include <pocketmage.h>

PocketmageCLOCK& clock = CLOCK();
```

`CLOCK()` is the single instance. `setupClock()` runs in `PocketMage_INIT()`; `wireClock()` performs the I2C wire-up the RTC needs and is called from there.

## Class reference

```cpp
class PocketmageCLOCK {
public:
  bool begin();
  void setTimeFromString(String timeStr);
  bool isValid();

  void setToCompileTimeUTC();
  DateTime nowDT();
  RTC_PCF8563& getRTC();

  long getTimeDiff();
  volatile long getTimeoutMillis() const;
  volatile long getPrevTimeMillis() const;
  void setTimeoutMillis(long t);
  void setPrevTimeMillis(long t);
};
```

### Reading and setting time

- `nowDT()` returns an `RTClib::DateTime` for draw code (`now.hour()`, `now.minute()`, `now.dayOfTheWeek()`, ...).
- `setTimeFromString("HH:MM")` sets the current day's time and rejects bad input with a `STR_INVALID` OLED message instead of a bad write.
- `setToCompileTimeUTC()` sets the clock from `__DATE__`/`__TIME__` (used by `SET_CLOCK_ON_UPLOAD`).
- `isValid()` returns false until `begin()` succeeds, and checks the RTC year is sane (2020..2099) to catch a reset or dead cell. `checkRTCPowerLoss()` (see [sys](sys.md)) re-baselines when the RTC has lost power.
- `getRTC()` hands out the raw driver for anything not covered.

### Timeout bookkeeping

The auto-sleep timer is a pair of millis stamps in `timeoutMillis_` / `prevTimeMillis_`. The OS calls `setPrevTimeMillis(millis())` on every user interaction (keypress, touch, SD activity poke) and compares with `getTimeDiff()` against the `TIMEOUT` setting to decide when to sleep. Reads never mutate; `setTimeoutMillis`/`setPrevTimeMillis` are the only writes. The same pair is reused by the touch driver to reset idle on touches, so a scroll keeps the device awake automatically.

## Example

```cpp
#include <pocketmage.h>

void drawClockLine() {
  DateTime now = CLOCK().nowDT();
  OLED().oledWord(String(now.hour()) + ":" +
                  String(now.minute() < 10 ? "0" : "") + String(now.minute()));
}
