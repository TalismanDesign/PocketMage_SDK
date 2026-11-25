#include "pocketmage_audio.h"

#include <Preferences.h>

#include "audio_settings_format.h"

namespace {
constexpr char kSettingsNamespace[] = "PMAudio";
constexpr char kSettingsKey[] = "settings";
} // namespace

void PocketMageAudio::setVolumeLevel(uint8_t level) {
  setVolumeLevelFor(volumeOutput(), level);
}

uint8_t PocketMageAudio::getVolumeLevel() const {
  return getVolumeLevelFor(volumeOutput());
}

void PocketMageAudio::setVolumeLevelFor(AudioOutput output, uint8_t level) {
  if (output == AudioOutput::Usb)
    _usbVolume = level;
  else if (output == AudioOutput::Piezo)
    _piezoVolume = level;
}

uint8_t PocketMageAudio::getVolumeLevelFor(AudioOutput output) const {
  return output == AudioOutput::Usb ? _usbVolume.load() : _piezoVolume.load();
}

AudioOutput PocketMageAudio::volumeOutput() const {
  // A started stream wins. Otherwise follow the sink the next frame would pick,
  // so the level shown before play is the one that plays.
  if (_usbStarted || (_output != AudioOutput::Piezo && _usbConnected))
    return AudioOutput::Usb;
  return AudioOutput::Piezo;
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
  uint8_t data[pm_audio_settings::kBytes] = {};
  const size_t length = storage.getBytesLength(kSettingsKey);
  const bool read =
      length <= sizeof(data) && storage.getBytes(kSettingsKey, data, length) == length;
  storage.end();
  pm_audio_settings::Settings settings;
  if (!read ||
      !pm_audio_settings::decode(data, length, static_cast<uint8_t>(AudioOutput::Auto), settings)) {
    setError("invalid audio settings");
    return false;
  }
  xSemaphoreTakeRecursive(_mutex, portMAX_DELAY);
  _piezoVolume = settings.piezoLevel;
  _usbVolume = settings.usbLevel;
  setOutput(static_cast<AudioOutput>(settings.output));
  _settingsLoaded = true;
  xSemaphoreGiveRecursive(_mutex);
  return true;
}

bool PocketMageAudio::saveSettings() {
  xSemaphoreTakeRecursive(_mutex, portMAX_DELAY);
  pm_audio_settings::Settings settings;
  settings.piezoLevel = _piezoVolume;
  settings.usbLevel = _usbVolume;
  settings.output = static_cast<uint8_t>(getOutput());
  uint8_t data[pm_audio_settings::kBytes];
  pm_audio_settings::encode(settings, data);
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
