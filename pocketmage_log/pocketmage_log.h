//  dP                           //
//  88                           //
//  88        .d8888b. .d8888b.  //
//  88        88'  `88 88'  `88  //
//  88        88.  .88 88.  .88  //
//  88888888P `88888P' `8888P88  //
//                          .88  //
//                      d8888P   //

#pragma once

#include <cstddef>
#include <cstdint>

#include "log_api.h"
#include "log_core.h"

/** The SDK logger:
 * Every line goes to the serial console and to a RAM tail that crash reports include. 
 * With file output on, lines are also queued and a low-priority task appends them to
 * /logs/system.txt, rotating it to /logs/system.1.txt when it grows past kFileLimitBytes.
 * Lines logged between begin() and the first setFileEnabled() call are held, so boot messages
 * reach the file too. 
 * ESP-IDF component logs are routed here once begin() has run.
 */
namespace pm_log {

/** Main log file. */
constexpr const char *kFilePath = "/logs/system.txt";
/** Previous log file, replaced on each rotation. */
constexpr const char *kRotatedPath = "/logs/system.1.txt";
/** Size at which kFilePath is rotated. */
constexpr size_t kFileLimitBytes = 256 * 1024;
/** RAM held for lines waiting to be written to the card. */
constexpr size_t kQueueBytes = 4 * 1024;
/** Newest log text kept for crash reports. */
constexpr size_t kTailBytes = 1024;

/** Install the ESP-IDF log hook and the restart flush. Safe to call more than once. */
void begin();

/** Turn file output on or off. The writer task is created the first time this enables output,
 * so call it only after the SD card is set up. Turning output off discards queued lines.
 */
void setFileEnabled(bool enabled);
/** Return whether file output is on. */
bool fileEnabled();

/** Set the most detailed level written for tag. A null tag or "*" sets the default for every
 * tag without its own entry, and keeps the entries already set. ESP-IDF tags are updated too.
 * Not for use from interrupts. Returns false for an invalid level, an empty tag, a tag longer
 * than kTagCapacity - 1, or a full table.
 */
bool setLevel(const char *tag, pm_log_level level);
/** Return the most detailed level written for tag. */
pm_log_level level(const char *tag);

/** Wait up to timeoutMs for queued lines to reach the card. Returns true when the queue is empty,
 * and false at once when file output is off or the card is unavailable.
 */
bool flush(uint32_t timeoutMs);

/** Copy the newest log text into out, oldest line first, NUL-terminated. Takes no locks, so a
 * panic handler may call it. Returns the length without NUL.
 */
size_t copyTail(char *out, size_t capacity);

} // namespace pm_log
