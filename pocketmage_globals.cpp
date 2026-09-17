#include <pocketmage_globals.h>
#include <pocketmage.h>

#include <esp_system.h>

#if OTA_APP
// In OS builds these symbols are owned by the OS (PocketMage_PDA src/globals.cpp).
// An OTA app links against the SDK alone, so the SDK provides them here.
SPIClass *vspi = nullptr;
SPIClass *hspi = nullptr;
fs::FS *global_fs = nullptr;

Preferences prefs;
AppState CurrentAppState = HOME;

int TIMEOUT = 120;
bool DEBUG_VERBOSE = true;
bool SYSTEM_CLOCK = true;
bool SHOW_YEAR = true;
bool SAVE_POWER = true;
bool ALLOW_NO_MICROSD = true;
bool HOME_ON_BOOT = false;
bool FAST_REFRESH = POCKETMAGE_HW_VERSION == 1;
int OLED_BRIGHTNESS = 255;
int OLED_MAX_FPS = 60;
bool MUTE_BUZZER = false;
bool SD_SPI_COMPATIBILITY = false;
#endif  // OTA_APP

namespace pocketmage {

void loadSettings() {
  prefs.begin("PocketMage", true);
  TIMEOUT = prefs.getInt("TIMEOUT", 120);
  DEBUG_VERBOSE = prefs.getBool("DEBUG_VERBOSE", true);
  SYSTEM_CLOCK = prefs.getBool("SYSTEM_CLOCK", true);
  SHOW_YEAR = prefs.getBool("SHOW_YEAR", true);
  SAVE_POWER = prefs.getBool("SAVE_POWER", true);
  ALLOW_NO_MICROSD = prefs.getBool("ALLOW_NO_SD", true);
  PM_SDAUTO().setEditingFile(prefs.getString("editingFile", ""));
  HOME_ON_BOOT = prefs.getBool("HOME_ON_BOOT", false);
  FAST_REFRESH = prefs.getBool("FAST_REFRESH", POCKETMAGE_HW_VERSION == 1);
  OLED_BRIGHTNESS = prefs.getInt("OLED_BRIGHTNESS", 255);
  OLED_MAX_FPS = prefs.getInt("OLED_MAX_FPS", 60);
  MUTE_BUZZER = prefs.getBool("MUTE_BUZZER", false);
  SD_SPI_COMPATIBILITY = prefs.getBool("SD_SPI_CMPT", false);
  I18n::setLanguage(static_cast<Lang>(prefs.getInt("Language", static_cast<int>(Lang::English))));
  prefs.end();
}

void recoverFromCrash() {
  esp_reset_reason_t reason = esp_reset_reason();
  if (reason != ESP_RST_PANIC && reason != ESP_RST_WDT &&
      reason != ESP_RST_TASK_WDT && reason != ESP_RST_INT_WDT) {
    return;
  }

  prefs.begin("PocketMage", false);
  prefs.putInt("CurrentAppState", HOME);
  prefs.end();
  PM_SDAUTO().setEditingFile("");
}

}  // namespace pocketmage
