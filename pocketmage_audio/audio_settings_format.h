#pragma once

#include <cstddef>
#include <cstdint>

/** NVS record layout for audio preferences: {version, piezo level, USB level, output}.
 * Versions 1 and 2 held one shared level, which becomes the piezo level on read
 * while USB starts at its own default.
 */
namespace pm_audio_settings {

constexpr uint8_t kVersion = 3;
constexpr uint8_t kSharedLevelVersion = 2;
constexpr uint8_t kGainStepVersion = 1;
/** Version 1 stored a 0..32 gain step instead of a 0..255 level. */
constexpr unsigned kGainSteps = 32;
constexpr unsigned kLevelMaximum = 255;
/** Approximately -18 dB; the piezo needs most of the range to be audible. */
constexpr uint8_t kDefaultPiezoLevel = 160;
/** Shown as 20% by level-percent displays; approximately -36 dB. */
constexpr uint8_t kDefaultUsbLevel = 51;
/** Bytes of the current record; older records are kLegacyBytes. */
constexpr size_t kBytes = 4;
constexpr size_t kLegacyBytes = 3;

/** Decoded preferences. output holds the raw AudioOutput value. */
struct Settings {
  uint8_t piezoLevel = kDefaultPiezoLevel;
  uint8_t usbLevel = kDefaultUsbLevel;
  uint8_t output = 0;
};

/** Encode settings as a current-version record into out[kBytes]. */
inline void encode(const Settings &settings, uint8_t (&out)[kBytes]) {
  out[0] = kVersion;
  out[1] = settings.piezoLevel;
  out[2] = settings.usbLevel;
  out[3] = settings.output;
}

/** Decode a record of length bytes. maxOutput is the largest valid AudioOutput.
 * Return false for an unknown version, wrong length or out-of-range field,
 * leaving out unchanged.
 */
inline bool decode(const uint8_t *data, size_t length, uint8_t maxOutput, Settings &out) {
  if (!data || length == 0)
    return false;
  Settings decoded;
  if (data[0] == kVersion) {
    if (length != kBytes || data[3] > maxOutput)
      return false;
    decoded.piezoLevel = data[1];
    decoded.usbLevel = data[2];
    decoded.output = data[3];
  } else if (data[0] == kSharedLevelVersion || data[0] == kGainStepVersion) {
    if (length != kLegacyBytes || data[2] > maxOutput)
      return false;
    unsigned level = data[1];
    if (data[0] == kGainStepVersion) {
      if (level > kGainSteps)
        return false;
      const unsigned normalized = (level * kLevelMaximum + kGainSteps / 2) / kGainSteps;
      // Old boosted defaults must not become unexpectedly loud at migration.
      level = normalized < kDefaultPiezoLevel ? normalized : kDefaultPiezoLevel;
    }
    decoded.piezoLevel = static_cast<uint8_t>(level);
    decoded.output = data[2];
  } else {
    return false;
  }
  out = decoded;
  return true;
}

} // namespace pm_audio_settings
