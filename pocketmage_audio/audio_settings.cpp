#include "pocketmage_audio.h"

#include <Preferences.h>

namespace {
constexpr char kSettingsNamespace[] = "PMAudio";
constexpr char kSettingsKey[] = "settings";
constexpr uint8_t kSettingsVersion = 1;
constexpr int kGainLimit = 16;
constexpr int kGainSteps = 2 * kGainLimit;
constexpr int kLevelMaximum = 255;
}

void PocketMageAudio::setVolumeLevel(uint8_t level) {
  setVolume(static_cast<int8_t>((level * kGainSteps + kLevelMaximum / 2) /
                               kLevelMaximum - kGainLimit));
}

uint8_t PocketMageAudio::getVolumeLevel() const {
  return static_cast<uint8_t>(((getVolume() + kGainLimit) * kLevelMaximum +
                               kGainSteps / 2) / kGainSteps);
}

bool PocketMageAudio::loadSettings() {
  // A local handle avoids sharing prefs.begin()/end() with the OS settings UI.
  // https://docs.espressif.com/projects/arduino-esp32/en/latest/api/preferences.html
  Preferences storage;
  if (!storage.begin(kSettingsNamespace, false)) {
    setError("audio settings unavailable");
    return false;
  }
  if (!storage.isKey(kSettingsKey)) {
    storage.end();
    _settingsLoaded = true;
    return true;
  }
  uint8_t data[3] = {};
  bool read = storage.getBytesLength(kSettingsKey) == sizeof(data) &&
              storage.getBytes(kSettingsKey, data, sizeof(data)) == sizeof(data);
  storage.end();
  if (!read || data[0] != kSettingsVersion || data[1] > kGainSteps ||
      data[2] > static_cast<uint8_t>(AudioOutput::Auto)) {
    setError("invalid audio settings");
    return false;
  }
  xSemaphoreTakeRecursive(_mutex, portMAX_DELAY);
  setVolume(static_cast<int8_t>(data[1] - kGainLimit));
  setOutput(static_cast<AudioOutput>(data[2]));
  _settingsLoaded = true;
  xSemaphoreGiveRecursive(_mutex);
  return true;
}

bool PocketMageAudio::saveSettings() {
  xSemaphoreTakeRecursive(_mutex, portMAX_DELAY);
  const uint8_t data[] = {kSettingsVersion,
                         static_cast<uint8_t>(getVolume() + kGainLimit),
                         static_cast<uint8_t>(getOutput())};
  Preferences storage;
  bool written = false;
  if (storage.begin(kSettingsNamespace, false)) {
    written = storage.putBytes(kSettingsKey, data, sizeof(data)) == sizeof(data);
    storage.end();
  }
  xSemaphoreGiveRecursive(_mutex);
  if (!written)
    setError("audio settings not saved");
  return written;
}
