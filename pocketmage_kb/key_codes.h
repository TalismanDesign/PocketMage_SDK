#pragma once

#include <cstdint>

/** Logical keyboard bytes shared by the matrix, HID translation and app bindings.
 * Up/Down are the existing Fn+Shift Left/Select controls; printable keys use ASCII.
 */
enum KeyCode : uint8_t {
  KeyNone = 0,
  KeyFnRight = 6,
  KeyFnSelect = 7,
  KeyBackspace = 8,
  KeyTab = 9,
  KeyEscape = 12,
  KeyEnter = 13,
  KeyBackTab = 14,
  KeyShift = 17,
  KeyFn = 18,
  KeyLeft = 19,
  KeySelect = 20,
  KeyRight = 21,
  KeyUp = 24,
  KeyDown = 25,
  KeyFnShiftRight = 26,
  KeyAsciiEscape = 27,
  KeyShiftLeft = 28,
  KeyShiftSelect = 29,
  KeyShiftRight = 30,
  KeySpace = 32,
};
