#ifndef POCKETMAGE_GLOBALS_H
#define POCKETMAGE_GLOBALS_H

#include <Arduino.h>
#include <FS.h>
#include <Preferences.h>
#include <SD.h>
#include <SD_MMC.h>
#include <SPI.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <pocketmage.h>

#include <config.h>

// ===================== SPI BUSES =====================
extern SPIClass *vspi;
extern SPIClass *hspi;
extern fs::FS *global_fs;

// ===================== PERSISTENT STORAGE =====================
extern Preferences prefs;

// ===================== SYSTEM STATE =====================
extern TaskHandle_t einkHandlerTaskHandle;  // E-ink refresh task (defined in pocketmage_eink.cpp)

// ===================== KEYBOARD STATE =====================
enum KBState { NORMAL, SHIFT, FUNC, FN_SHIFT };

// ===================== APP STATES =====================
enum AppState { HOME, TXT, FILEWIZ, USB_APP, COMM, SETTINGS, TASKS, CALENDAR, JOURNAL, LEXICON, APPLOADER, TERMINAL, ONBOARDING };
extern AppState CurrentAppState;

// ===================== BATTERY HELPER =====================
inline float getBatteryVoltage() {
  return (analogRead(BAT_SENS) * (3.3 / 4095.0) * 2) + 0.2;
}

namespace pocketmage {

// Load persisted settings and the UI language from NVS into the globals
// declared in config.h. Boot app dispatch stays with the OS.
void loadSettings();

// Reset persisted app state after an abnormal reset (panic/watchdog).
// Prompt-free so it is safe to call from an OTA app before APP_INIT().
void recoverFromCrash();

// Baseline the RTC when it has lost power.
void checkRTCPowerLoss();

}  // namespace pocketmage

// Entry points supplied by the linked app.
#if OTA_APP
void APP_INIT();
void processKB_APP();
void einkHandler_APP();
#endif

// Boot hooks provided by the PocketMage OS when the SDK is linked into it.
// See PocketMage_PDA src/UTILS.cpp. In OTA builds the SDK supplies its own
// equivalents (pocketmage::loadSettings / pocketmage::recoverFromCrash).
#if !OTA_APP
void loadState(bool changeState = true, char bootKey = 0);
void checkCrashState();
void checkRTCPowerLoss();
void applicationEinkHandler();
void processKB();
#endif

#endif  // POCKETMAGE_GLOBALS_H
