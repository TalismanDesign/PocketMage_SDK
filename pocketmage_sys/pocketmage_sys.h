//  888888ba                    dP                  dP                                          //
//  88    `8b                   88                  88                                          //
// a88aaaa8P' .d8888b. .d8888b. 88  .dP  .d8888b. d8888P 88d8b.d8b. .d8888b. .d8888b. .d8888b.  //
//  88        88'  `88 88'  `"" 88888"   88ooood8   88   88'`88'`88 88'  `88 88'  `88 88ooood8  //
//  88        88.  .88 88.  ... 88  `8b. 88.  ...   88   88  88  88 88.  .88 88.  .88 88.  ...  //
//  dP        `88888P' `88888P' dP   `YP `88888P'   dP   dP  dP  dP `88888P8 `8888P88 `88888P'  //
//                                                                                .88           //
//                                                                            d8888P            //

#pragma once
#include <Arduino.h>
#include <vector>

class String;

extern bool mscEnabled;
extern bool sinkEnabled;
extern volatile bool SDActive;
extern volatile int battState;      // Battery state
extern volatile bool PWR_BTN_event; // Power button event **shared with library**

namespace pocketmage {
void setCpuSpeed(int newFreq);
/** Record native or ELF input activity using the OS monotonic clock. */
void reportActivity();
/** Poll the shared TIMEOUT interval; inhibition restarts the inactivity clock. */
bool idleSleepDue(bool inhibited);
/** OS idle manager: preserve editing state and enter the configured deep sleep. */
void checkIdleSleep();
void deepSleep(bool alternateScreenSaver = false);
void IRAM_ATTR PWR_BTN_irq();

class ScopedCpuBoost {
public:
  ScopedCpuBoost();
  ~ScopedCpuBoost();
  ScopedCpuBoost(const ScopedCpuBoost &) = delete;
  ScopedCpuBoost &operator=(const ScopedCpuBoost &) = delete;

private:
  int prevFreq_ = 240;
};
} // namespace pocketmage

// ===================== SYSTEM SETUP =====================
void PocketMage_INIT();
// ===================== GLOBAL TEXT HELPERS =====================
String vectorToString();
void stringToVector(String inputText);

extern volatile bool resetRequested;
extern volatile bool newLineAdded;   // New line added in TXT
extern std::vector<String> allLines; // All lines in TXT
extern bool noTimeout;               // Disable timeout
extern void setLoadSwitch(bool state);
