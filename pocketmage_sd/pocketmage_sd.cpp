//  .d88888b  888888ba   //
//  88.    "' 88    `8b  //
//  `Y88888b. 88     88  //
//        `8b 88     88  //
//  d8'   .8P 88    .8P  //
//   Y88888P  8888888P   //

#include <config.h>
#include <pocketmage.h>
#include <pocketmage_globals.h>

#include <cstdio>
#include <cstring>

#include "driver/sdmmc_host.h"
#include "driver/sdspi_host.h"
#include "esp_vfs_fat.h"

#include "sd_gateway.h"

static constexpr const char *TAG = "SD";

extern bool SAVE_POWER;

static PocketmageSD pm_sd;

PocketmageSD &PM_SD() {
  return pm_sd;
}
PocketmageSD &PM_SDAUTO() {
  return pm_sd;
}

void PocketmageSD::beginIO() {
  SDActive = true;
}
void PocketmageSD::endIO() {
  SDActive = false;
}

// Resolve a relative path (e.g. "/music") to a full VFS path (e.g. "/sdcard/music").
// Paths already starting with the mount point are returned as-is.
const char *PocketmageSD::sdPath(const char *relPath) {
  static char buf[kMaxPathLen];
  if (strncmp(relPath, kSdMountPoint, strlen(kSdMountPoint)) == 0) {
    return relPath;
  }
  snprintf(buf, kMaxPathLen, "%s%s", kSdMountPoint, relPath);
  return buf;
}

// The raw filesystem helpers below funnel through the SD gateway task, so
// they are safe to call from any task and always run at 240MHz. Paths may be
// relative (the mount prefix is applied by the gateway) or already prefixed.

bool PocketmageSD::sdExists(const char *path) {
  pm_sd_gateway::Request r{};
  r.op = pm_sd_gateway::Op::Stat;
  r.path = path;
  pm_sd_gateway::submit(&r);
  return r.ok;
}

bool PocketmageSD::sdMkdir(const char *path) {
  pm_sd_gateway::Request r{};
  r.op = pm_sd_gateway::Op::Mkdir;
  r.path = path;
  pm_sd_gateway::submit(&r);
  return r.ok;
}

bool PocketmageSD::sdRemove(const char *path) {
  pm_sd_gateway::Request r{};
  r.op = pm_sd_gateway::Op::Remove;
  r.path = path;
  pm_sd_gateway::submit(&r);
  return r.ok;
}

bool PocketmageSD::sdRename(const char *path1, const char *path2) {
  pm_sd_gateway::Request r{};
  r.op = pm_sd_gateway::Op::Rename;
  r.path = path1;
  r.path2 = path2;
  pm_sd_gateway::submit(&r);
  return r.ok;
}

bool PocketmageSD::sdGetFileInfo(const char *path, size_t *size) {
  pm_sd_gateway::Request r{};
  r.op = pm_sd_gateway::Op::Stat;
  r.path = path;
  pm_sd_gateway::submit(&r);
  if (r.ok && size)
    *size = r.retVal;
  return r.ok;
}

static int countVisibleCharsFile(const char *path) {
  int slot = PM_SD().streamOpen(path, 'r');
  if (slot < 0)
    return 0;

  int count = 0;
  uint8_t buf[512];

  size_t len;
  while ((len = PM_SD().streamRead(slot, buf, sizeof(buf))) > 0) {
    for (size_t i = 0; i < len; i++) {
      if (buf[i] >= 32 && buf[i] <= 126) {
        count++;
      }
    }
    vTaskDelay(1);
  }

  PM_SD().streamClose(slot);
  return count;
}

static const char *GUIDE_BACKGROUND =
    "How to add custom backgrounds:\n"
    "1. Make a background that is 1 bit (black OR white) and 320x240 pixels.\n"
    "2. Export your background as a .bmp file.\n"
    "3. Use image2cpp to convert your image to a .bin file.\n"
    "   Settings: Invert Image Colors = TRUE, Swap Bits in Byte = FALSE.\n"
    "4. Place the .bin file in this folder.\n"
    "5. Enjoy your new custom wallpapers!";

static const char *GUIDE_COMMANDS =
    "# PocketMage Keystrokes Guide\n"
    "This is a guide on common key combinations and commands on the PocketMage PDA device. "
    "The guide is split up into sections based on application.\n"
    "\n"
    "---\n"
    "## General Keystrokes (work in almost any app)\n"
    "- (FN) + ( < ) | Exit or back button\n"
    "- (FN) + ( > ) | Save document\n"
    "- (FN) + ( o ) | Clear Line\n"
    "- (FN) + (Key) | FN layer keymapping (legends on the PCB)\n"
    "- (SHFT) + (key) | Capital letter\n"
    "- ( o ) OR (ENTER) | Select button\n"
    "\n"
    "---\n"
    "## While Sleeping\n"
    "### Bypass home and directly enter an app\n"
    "You can bypass the home menu and enter directly into an app and wake up with one keystroke. "
    "Pressing the buttons below while PocketMage is sleeping will wake the device and boot into "
    "the corresponding app.\n"
    "\n"
    "- ( SPACE ) - Return to previous app (saved state from last sleep)\n"
    "- ( H ) - Home\n"
    "- ( U ) - USB\n"
    "- ( F ) - Filewiz\n"
    "- ( T ) - Tasks\n"
    "- ( N ) - TXT\n"
    "- ( S ) - Settings\n"
    "- ( C ) - Calendar\n"
    "- ( J ) - Journal\n"
    "- ( D ) - Dictionary (lexicon)\n"
    "- ( L ) - Loader\n"
    "\n"
    "---\n"
    "## Home App\n"
    "### Entering an OS app\n"
    "Type an app's name to enter that app. For example, to enter calendar, type \"calendar\". "
    "You can type the name as it appears on the screen or use a shortcut. "
    "For example, typing \"cal\" also enters calendar.\n"
    "\n"
    "### Entering a 3rd party app\n"
    "Type the letter of the slot that app is installed in. "
    "For example if you have the Calc app installed in the first app slot, type \"a\" to enter the "
    "app.\n"
    "\n"
    "### Other commands\n"
    "Many other commands can be done from the homescreen, including all of the settings commands "
    "and some other fun ones for you to discover!\n"
    "\n"
    "---\n"
    "## TXT App\n"
    "- (FN) + ( < ) | Exit app\n"
    "- (FN) + ( > ) | Save document\n"
    "- (FN) + ( o ) | Enter filesystem (loading files)\n"
    "- (SHFT) + ( o ) | New blank text document\n"
    "- (FN) + (Key) | FN layer keymapping (legends on the PCB)\n"
    "- (SHFT) + (key) | Capital letter\n"
    "- (ENTER) | Create a new line\n"
    "- (SHFT) + ( < ) | Change text style (body, heading, etc.)\n"
    "- (SHFT) + ( > ) | Change formatting (bold, italic, etc.)\n"
    "- Scroll Bar | Swipe up and down to scroll through the document\n"
    "\n"
    "---\n"
    "## FILEWIZ\n"
    "- (FN) + ( < ) | Exit app\n"
    "- ( < ) AND ( > ) | Scroll left and right\n"
    "- ( o ) OR (ENTER) | Select file or folder\n"
    "- ( 0 ) TO ( 9 ) | Select recent file\n"
    "- ( BKSP ) | Go back a filesystem level\n"
    "\n"
    "---\n"
    "## USB\n"
    "Plug in the PocketMage to your PC to view the files. Eject and exit the app when you're "
    "finished.\n"
    "- (FN) + ( < ) | Exit app\n"
    "\n"
    "---\n"
    "## Settings\n"
    "Type the setting as it appears on the screen to change it. Some examples are given below. "
    "Note: all settings are case-insensitive, meaning that you can type in all lowercase. "
    "All of these settings are also available from the home menu command bar if you memorize "
    "them.\n"
    "- TimeSet [HH]:[MM] -> TimeSet 15:46\n"
    "- DateSet YYYYMMDD -> DateSet 20251230\n"
    "- ShowYear [bool] -> ShowYear t\n"
    "- Timeout [int] -> Timeout 300\n"
    "- (FN) + ( < ) | Exit app\n"
    "\n"
    "---\n"
    "## Tasks\n"
    "- ( N ) | Create a new task, follow on-screen prompts\n"
    "- (ENTER) | Enter information into prompt\n"
    "- ( 0 ) TO ( 9 ) | Select task for editing\n"
    "- (FN) + ( < ) | Exit app\n"
    "\n"
    "---\n"
    "## Calendar\n"
    "Type commands to navigate dates or create events. Commands are case-insensitive.\n"
    "\n"
    "### Month View\n"
    "- jan 2025 / feb 2030 / etc. | Jump to month and year\n"
    "- 20251225 | Jump to exact date (YYYYMMDD)\n"
    "- 14 | Jump to a day in the current week\n"
    "- ( N ) | New event\n"
    "- (FN) + ( < ) | Exit app\n"
    "\n"
    "### Week View\n"
    "- sun, mon, tue, wed, thu, fri, sat | Jump to weekday in the viewed week\n"
    "- ( N ) | New event\n"
    "- (FN) + ( < ) | Exit app\n"
    "\n"
    "### Day View\n"
    "- ( N ) | New event for selected day\n"
    "- 1, 2, 3, ... | Open event by index\n"
    "- (FN) + ( < ) | Exit app\n"
    "\n"
    "### Repeating Events\n"
    "- no | No repeat\n"
    "- daily | Repeat every day\n"
    "- weekly xx | Repeat every week, xx is one or more of mo, tu, we, th, fr, sa, su\n"
    "- monthly xx | Repeat monthly, xx is the day of the month (1-31) or ordinal weekday (ex. "
    "2tu)\n"
    "- yearly xx | Repeat every year, xx is month and day of the month (ex. apr22)\n"
    "\n"
    "---\n"
    "## Journal\n"
    "Type a date to open or create a journal entry. Commands are case-insensitive.\n"
    "- ( T ) | Open today's journal entry\n"
    "- YYYYMMDD - Example: 20250314 | Open/create entry for exact date\n"
    "- jan 1 / feb 12 / etc. | Open/create entry for given month and day (uses current year)\n"
    "- (FN) + ( < ) | Exit app\n"
    "\n"
    "---\n"
    "## Lexicon\n"
    "Type a word to search the dictionary. Matches are loaded from the SD card. Commands are "
    "case-insensitive.\n"
    "- Type any word | Search for definitions (example: abandon)\n"
    "- (ENTER) | Execute search\n"
    "- ( < ) OR ( > ) | Previous / next definition\n"
    "- (FN) + ( < ) | Exit app\n"
    "\n"
    "---\n"
    "## App loader\n"
    "Manage and install .tar apps to OTA slots. Commands are case-insensitive.\n"
    "- ( S ) | Swap app in selected slot (choose a .tar file)\n"
    "- ( D ) | Delete app in selected slot\n"
    "- (FN) + ( < ) | Exit app / return to menu\n"
    "- Progress Bar | Shows extraction (0-50%) and installation (50-100%) status\n"
    "\n"
    "---\n"
    "## Sleep Modes\n"
    "When on battery, save power and look at a random screensaver. "
    "When charging, view a clock, upcoming tasks, and weather (work in progress)\n"
    "### Sleep (when not plugged into usb)\n"
    "- sleep button to enter sleep\n"
    "- any key on keyboard to wake\n"
    "### Now-Later (when usb is plugged in)\n"
    "- sleep button to enter now-later\n"
    "- sleep button to wake\n";

static void provisionFilesystem() {
  const char *dirs[] = {"/sys",  "/notes",     "/journal", "/dict",
                        "/apps", "/apps/temp", "/assets",  "/assets/backgrounds",
                        "/chats"};
  for (auto dir : dirs) {
    if (!PM_SD().sdExists(dir))
      PM_SD().sdMkdir(dir);
  }

  const char *howtoPath = "/assets/backgrounds/HOWTOADDBACKGROUNDS.txt";
  if (!PM_SD().sdExists(howtoPath)) {
    PM_SD().writeFile(howtoPath, GUIDE_BACKGROUND);
  }

  const char *manualPath = "/sys/COMMAND_MANUAL.txt";
  if (!PM_SD().sdExists(manualPath)) {
    PM_SD().writeFile(manualPath, GUIDE_COMMANDS);
  }

  const char *sysFiles[] = {"/sys/events.txt", "/sys/tasks.txt", "/sys/SDMMC_META.txt"};
  for (auto file : sysFiles) {
    if (!PM_SD().sdExists(file)) {
      PM_SD().writeFile(file, "");
    }
  }
}

void setupSD() {
  prefs.begin("PocketMage", true);
  SD_SPI_COMPATIBILITY = prefs.getBool("SD_SPI_CMPT", false);
  ALLOW_NO_MICROSD = prefs.getBool("ALLOW_NO_SD", true);
  prefs.end();
  Serial.print("SD_SPI_CMPT" + String(SD_SPI_COMPATIBILITY));
  delay(100);

  if (!SD_SPI_COMPATIBILITY) {
    PM_SD().setMode(PocketmageSD::SDMMC);

    pocketmage::setCpuSpeed(240);

    sdmmc_host_t host = SDMMC_HOST_DEFAULT();
    host.command_timeout_ms = 500;

    sdmmc_slot_config_t slot_config = SDMMC_SLOT_CONFIG_DEFAULT();
#if POCKETMAGE_HW_VERSION == 2
    slot_config.width = 4;
    slot_config.clk = (gpio_num_t)SD_CLK;
    slot_config.cmd = (gpio_num_t)SD_CMD;
    slot_config.d0 = (gpio_num_t)SD_D0;
    slot_config.d1 = (gpio_num_t)SD_D1;
    slot_config.d2 = (gpio_num_t)SD_D2;
    slot_config.d3 = (gpio_num_t)SD_D3;
#else
    slot_config.width = 1;
    slot_config.clk = (gpio_num_t)SD_CLK;
    slot_config.cmd = (gpio_num_t)SD_CMD;
    slot_config.d0 = (gpio_num_t)SD_D0;
    slot_config.d1 = (gpio_num_t)-1;
    slot_config.d2 = (gpio_num_t)-1;
    slot_config.d3 = (gpio_num_t)-1;
#endif

    esp_vfs_fat_sdmmc_mount_config_t mount_config = {};
    mount_config.format_if_mount_failed = false;
    mount_config.max_files = 5;
    mount_config.allocation_unit_size = 16 * 1024;

    bool sdOK = false;
    for (int attempt = 1; attempt <= 25; attempt++) {
      sdmmc_card_t *card = nullptr;
      esp_err_t ret = esp_vfs_fat_sdmmc_mount("/sdcard", &host, &slot_config, &mount_config, &card);
      if (ret == ESP_OK) {
        delay(120);
        sdOK = true;
        break;
      }
      esp_vfs_fat_sdmmc_unmount();
      delay(200);
    }

    if (!sdOK) {
      ESP_LOGE(TAG, "MOUNT FAILED");
      delay(5000);
      if (ALLOW_NO_MICROSD) {
        OLED().oledWord(TR(STR_SD_ALL_WORK_LOST), 5000);
        PM_SD().setNoSD(true);
        return;
      } else {
        OLED().oledWord(TR(STR_SD_INSERT_REBOOT), 5000);
        OLED().setPowerSave(1);
        BZ().playJingle(Jingles::Shutdown);
        esp_deep_sleep_start();
        return;
      }
    }

    PM_SD().setNoSD(false);

    prefs.begin("PocketMage", false);
    prefs.putBool("SD_SPI_CMPT", false);
    prefs.end();

    provisionFilesystem();

    // All card I/O from here on goes through the gateway task.
    pm_sd_gateway::start();
  } else {
    PM_SD().setMode(PocketmageSD::SDSPI);

    pocketmage::setCpuSpeed(240);

    hspi = new SPIClass(HSPI);
    hspi->begin(SD_CLK, SD_MISO, SD_MOSI, SD_CS);
    pinMode(hspi->pinSS(), OUTPUT);

    sdmmc_host_t host = SDSPI_HOST_DEFAULT();
    host.command_timeout_ms = 500;

    sdmmc_slot_config_t slot_config = SDMMC_SLOT_CONFIG_DEFAULT();
    slot_config.width = 1;
    slot_config.clk = (gpio_num_t)SD_CLK;
    slot_config.cmd = (gpio_num_t)SD_CMD;
    slot_config.d0 = (gpio_num_t)SD_MISO;
    slot_config.d1 = (gpio_num_t)-1;
    slot_config.d2 = (gpio_num_t)-1;
    slot_config.d3 = (gpio_num_t)-1;
    slot_config.cd = (gpio_num_t)SD_CS;
    slot_config.wp = (gpio_num_t)-1;

    esp_vfs_fat_sdmmc_mount_config_t mount_config = {};
    mount_config.format_if_mount_failed = false;
    mount_config.max_files = 5;
    mount_config.allocation_unit_size = 16 * 1024;

    sdmmc_card_t *card = nullptr;
    esp_err_t ret = esp_vfs_fat_sdmmc_mount("/sdcard", &host, &slot_config, &mount_config, &card);
    if (ret != ESP_OK) {
      ESP_LOGE(TAG, "SPI SD Mount Failed");
      OLED().oledWord(TR(STR_SD_SPI_NOT_DETECTED), false, false);
      delay(2000);

      if (ALLOW_NO_MICROSD) {
        OLED().oledWord(TR(STR_SD_ALL_WORK_LOST), false, false);
        delay(5000);
        PM_SD().setNoSD(true);
        return;
      } else {
        OLED().oledWord(TR(STR_SD_COMPAT_FAILED), false, false);
        prefs.begin("PocketMage", false);
        prefs.putBool("SD_SPI_CMPT", false);
        prefs.end();
        delay(2000);
        OLED().setPowerSave(1);
        BZ().playJingle(Jingles::Shutdown);
        esp_deep_sleep_start();
        return;
      }
    }

    PM_SD().setNoSD(false);
    OLED().oledWord(TR(STR_SD_COMPAT_STARTED));

    // All card I/O from here on goes through the gateway task.
    pm_sd_gateway::start();
  }
}

// ===================== high-level operations =====================

void PocketmageSD::saveFile() {
  if (noSD_) {
    OLED().sysMessage(TR(STR_SD_SAVE_FAILED), 5000);
    return;
  }
  beginIO();

  String textToSave = vectorToString();
  ESP_LOGV(TAG, "Text to save: %s", textToSave.c_str());

  if (editingFile_ == "" || editingFile_ == "-")
    editingFile_ = "/temp.txt";
  keypad.disableInterrupts();
  if (!editingFile_.startsWith("/"))
    editingFile_ = "/" + editingFile_;
  writeFile(editingFile_.c_str(), textToSave.c_str());

  writeMetadata(editingFile_.c_str());

  keypad.enableInterrupts();
  endIO();
}

// Line splitter over a String buffer; trims CR and surrounding spaces so the
// metadata rewrite treats CRLF and stray whitespace like the old fgets loop.
static bool nextLine(const String &content, int &pos, String &out) {
  if (pos >= (int)content.length())
    return false;
  int nl = content.indexOf('\n', pos);
  if (nl < 0) {
    out = content.substring(pos);
    pos = content.length();
  } else {
    out = content.substring(pos, nl);
    pos = nl + 1;
  }
  out.trim();
  return true;
}

void PocketmageSD::writeMetadata(const char *path) {
  beginIO();

  size_t fileSizeBytes = 0;
  bool isDir = false;
  if (!statPath(path, &fileSizeBytes, &isDir) || isDir) {
    OLED().sysMessage(TR(STR_SD_META_WRITE_ERR), 1000);
    ESP_LOGE(TAG, "Invalid file for metadata: %s", path);
    endIO();
    return;
  }

  String fileSizeStr = String(fileSizeBytes) + " Bytes";

  int charCount = countVisibleCharsFile(path);
  String charStr = String(charCount) + " Char";

  DateTime now = CLOCK().nowDT();
  char timestamp[20];
  sprintf(timestamp, "%04d%02d%02d-%02d%02d", now.year(), now.month(), now.day(), now.hour(),
          now.minute());

  String newEntry = String(path) + "|" + timestamp + "|" + fileSizeStr + "|" + charStr;

  String metaContent;
  pm_sd_gateway::Request r{};
  r.op = pm_sd_gateway::Op::ReadFileString;
  r.path = SYS_METADATA_FILE;
  r.strOut = &metaContent;
  pm_sd_gateway::submit(&r);

  String updatedMeta = "";
  bool replaced = false;

  int pos = 0;
  String line;
  while (nextLine(metaContent, pos, line)) {
    if (line.startsWith(String(path) + "|")) {
      updatedMeta += newEntry + "\n";
      replaced = true;
    } else if (line.length() > 1) {
      updatedMeta += line + "\n";
    }
  }

  if (!replaced) {
    updatedMeta += newEntry + "\n";
  }

  pm_sd_gateway::Request w{};
  w.op = pm_sd_gateway::Op::WriteFile;
  w.path = SYS_METADATA_FILE;
  w.text = updatedMeta.c_str();
  pm_sd_gateway::submit(&w);
  ESP_LOGI(TAG, "Metadata updated");

  endIO();
}

void PocketmageSD::loadFile(bool showOLED) {
  if (noSD_) {
    OLED().sysMessage(TR(STR_SD_LOAD_FAILED), 5000);
    return;
  }
  beginIO();

  keypad.disableInterrupts();
  if (showOLED)
    OLED().oledWord(TR(STR_SD_LOADING_FILE));
  if (!editingFile_.startsWith("/"))
    editingFile_ = "/" + editingFile_;
  String textToLoad = readFileToString(editingFile_.c_str());
  ESP_LOGV(TAG, "Text to load: %s", textToLoad.c_str());

  stringToVector(textToLoad);
  keypad.enableInterrupts();
  if (showOLED) {
    OLED().oledWord(TR(STR_SD_FILE_LOADED));
    delay(200);
  }
  endIO();
}

void PocketmageSD::delFile(const char *fileName) {
  if (noSD_) {
    OLED().sysMessage(TR(STR_SD_DELETE_FAILED), 5000);
    return;
  }
  beginIO();

  keypad.disableInterrupts();
  String fn = fileName;
  if (!fn.startsWith("/"))
    fn = "/" + fn;
  deleteFile(fn.c_str());

  deleteMetadata(fn.c_str());

  delay(1000);
  keypad.enableInterrupts();
  endIO();
}

void PocketmageSD::deleteMetadata(const char *path) {
  beginIO();

  String metaContent;
  pm_sd_gateway::Request r{};
  r.op = pm_sd_gateway::Op::ReadFileString;
  r.path = SYS_METADATA_FILE;
  r.strOut = &metaContent;
  pm_sd_gateway::submit(&r);

  String updatedMeta = "";
  int pos = 0;
  String line;
  while (nextLine(metaContent, pos, line)) {
    if (!line.startsWith(String(path) + "|") && line.length() > 0) {
      updatedMeta += line + "\n";
    }
  }

  pm_sd_gateway::Request w{};
  w.op = pm_sd_gateway::Op::WriteFile;
  w.path = SYS_METADATA_FILE;
  w.text = updatedMeta.c_str();
  pm_sd_gateway::submit(&w);
  ESP_LOGI(TAG, "Metadata entry deleted (if it existed).");

  endIO();
}

void PocketmageSD::renFile(const char *oldFile, const char *newFile) {
  if (noSD_) {
    OLED().sysMessage(TR(STR_SD_RENAME_FAILED), 5000);
    return;
  }
  beginIO();

  keypad.disableInterrupts();
  String oldF = oldFile;
  String newF = newFile;
  if (!oldF.startsWith("/"))
    oldF = "/" + oldF;
  if (!newF.startsWith("/"))
    newF = "/" + newF;
  renameFile(oldF.c_str(), newF.c_str());
  OLED().sysMessage(oldF + " -> " + newF, 1000);

  renMetadata(oldF.c_str(), newF.c_str());

  keypad.enableInterrupts();
  endIO();
}

void PocketmageSD::renMetadata(const char *oldPath, const char *newPath) {
  beginIO();

  String metaContent;
  pm_sd_gateway::Request r{};
  r.op = pm_sd_gateway::Op::ReadFileString;
  r.path = SYS_METADATA_FILE;
  r.strOut = &metaContent;
  pm_sd_gateway::submit(&r);

  String updatedMeta = "";
  int pos = 0;
  String line;
  while (nextLine(metaContent, pos, line)) {
    if (line.startsWith(String(oldPath) + "|")) {
      int separatorIndex = line.indexOf('|');
      if (separatorIndex != -1) {
        String rest = line.substring(separatorIndex);
        line = String(newPath) + rest;
      } else {
        line = newPath;
      }
    }
    updatedMeta += line + "\n";
  }

  pm_sd_gateway::Request w{};
  w.op = pm_sd_gateway::Op::WriteFile;
  w.path = SYS_METADATA_FILE;
  w.text = updatedMeta.c_str();
  pm_sd_gateway::submit(&w);
  ESP_LOGI(TAG, "Metadata updated for renamed file.");

  endIO();
}

void PocketmageSD::copyFile(const char *oldFile, const char *newFile) {
  if (noSD_) {
    OLED().sysMessage(TR(STR_SD_COPY_FAILED), 5000);
    return;
  }
  beginIO();

  keypad.disableInterrupts();
  OLED().oledWord(TR(STR_SD_LOADING_FILE));
  String oldF = oldFile;
  String newF = newFile;
  if (!oldF.startsWith("/"))
    oldF = "/" + oldF;
  if (!newF.startsWith("/"))
    newF = "/" + newF;
  String textToLoad = readFileToString(oldF.c_str());
  writeFile(newF.c_str(), textToLoad.c_str());
  OLED().oledWord(String(TR(STR_SAVED_PREFIX)) + newF);

  writeMetadata(newF.c_str());

  delay(1000);
  keypad.enableInterrupts();

  endIO();
}

void PocketmageSD::appendToFile(const char *path, const char *inText) {
  if (noSD_) {
    OLED().sysMessage(TR(STR_SD_OP_FAILED), 5000);
    return;
  }
  beginIO();

  keypad.disableInterrupts();
  appendFile(path, inText);

  writeMetadata(path);

  keypad.enableInterrupts();

  endIO();
}

// ===================== low-level operations =====================

void PocketmageSD::listDir(const char *dirname) {
  if (noSD_) {
    OLED().sysMessage(TR(STR_SD_OP_FAILED), 5000);
    return;
  }
  ESP_LOGI(tag, "Listing directory %s\r\n", dirname);

  fileIndex_ = 0;
  for (int i = 0; i < MAX_FILES; i++) {
    filesList_[i] = "-";
  }

  int count = 0;
  pm_sd_gateway::Request r{};
  r.op = pm_sd_gateway::Op::ListDir;
  r.path = dirname;
  r.strOut = filesList_;
  r.cap = MAX_FILES;
  r.countOut = &count;
  r.excluded = &excludedFiles_;
  pm_sd_gateway::submit(&r);

  fileIndex_ = r.ok ? count : 0;
}

void PocketmageSD::readFile(const char *path) {
  if (noSD_) {
    OLED().sysMessage(TR(STR_SD_OP_FAILED), 5000);
    return;
  }
  ESP_LOGI(tag, "Reading file %s\r\n", path);

  if (!exists(path)) {
    ESP_LOGE(tag, "Failed to open file for reading: %s", path);
  }
}

String PocketmageSD::readFileToString(const char *path) {
  if (noSD_) {
    OLED().sysMessage(TR(STR_SD_OP_FAILED), 5000);
    return "";
  }
  ESP_LOGI(tag, "Reading file: %s\r\n", path);

  String content;
  pm_sd_gateway::Request r{};
  r.op = pm_sd_gateway::Op::ReadFileString;
  r.path = path;
  r.strOut = &content;
  pm_sd_gateway::submit(&r);

  if (!r.ok) {
    ESP_LOGE(tag, "Failed to open file for reading: %s", path);
    OLED().sysMessage(TR(STR_SD_LOAD_FAILED_SHORT), 500);
    return "";
  }

  if (content.length() > 0) {
    EINK().setFullRefreshAfter(FULL_REFRESH_AFTER);
  }
  return content;
}

void PocketmageSD::writeFile(const char *path, const char *message) {
  if (noSD_) {
    OLED().sysMessage(TR(STR_SD_OP_FAILED), 5000);
    return;
  }
  ESP_LOGI(tag, "Writing file: %s\r\n", path);

  pm_sd_gateway::Request r{};
  r.op = pm_sd_gateway::Op::WriteFile;
  r.path = path;
  r.text = message;
  pm_sd_gateway::submit(&r);

  if (r.ok) {
    ESP_LOGV(tag, "File written %s", path);
  } else {
    ESP_LOGE(tag, "Write failed for %s", path);
  }
}

void PocketmageSD::appendFile(const char *path, const char *message) {
  if (noSD_) {
    OLED().sysMessage(TR(STR_SD_OP_FAILED), 5000);
    return;
  }
  ESP_LOGI(tag, "Appending to file: %s\r\n", path);

  pm_sd_gateway::Request r{};
  r.op = pm_sd_gateway::Op::AppendFile;
  r.path = path;
  r.text = message;
  pm_sd_gateway::submit(&r);

  if (r.ok) {
    ESP_LOGV(tag, "Message appended to %s", path);
  } else {
    ESP_LOGE(tag, "Append failed: %s", path);
  }
}

void PocketmageSD::renameFile(const char *path1, const char *path2) {
  if (noSD_) {
    OLED().sysMessage(TR(STR_SD_OP_FAILED), 5000);
    return;
  }
  ESP_LOGI(tag, "Renaming file %s to %s\r\n", path1, path2);

  pm_sd_gateway::Request r{};
  r.op = pm_sd_gateway::Op::Rename;
  r.path = path1;
  r.path2 = path2;
  pm_sd_gateway::submit(&r);

  if (r.ok) {
    ESP_LOGV(tag, "Renamed %s to %s\r\n", path1, path2);
  } else {
    ESP_LOGE(tag, "Rename failed: %s to %s", path1, path2);
  }
}

void PocketmageSD::deleteFile(const char *path) {
  if (noSD_) {
    OLED().sysMessage(TR(STR_SD_OP_FAILED), 5000);
    return;
  }
  ESP_LOGI(tag, "Deleting file: %s\r\n", path);

  pm_sd_gateway::Request r{};
  r.op = pm_sd_gateway::Op::Remove;
  r.path = path;
  pm_sd_gateway::submit(&r);

  if (r.ok) {
    ESP_LOGV(tag, "File deleted: %s", path);
  } else {
    ESP_LOGE(tag, "Delete failed for %s", path);
  }
}

bool PocketmageSD::readBinaryFile(const char *path, uint8_t *buf, size_t len) {
  if (noSD_) {
    OLED().sysMessage(TR(STR_SD_OP_FAILED), 5000);
    return false;
  }

  pm_sd_gateway::Request r{};
  r.op = pm_sd_gateway::Op::ReadBinary;
  r.path = path;
  r.rbuf = buf;
  r.len = len;
  pm_sd_gateway::submit(&r);

  if (!r.ok) {
    ESP_LOGE(tag, "Read failed for %s (%u of %u bytes)", path, (unsigned)r.retVal, (unsigned)len);
  }
  return r.ok;
}

bool PocketmageSD::appendBinaryFile(const char *path, const uint8_t *buf, size_t len) {
  if (noSD_) {
    OLED().sysMessage(TR(STR_SD_OP_FAILED), 5000);
    return false;
  }

  pm_sd_gateway::Request r{};
  r.op = pm_sd_gateway::Op::AppendBinary;
  r.path = path;
  r.wbuf = buf;
  r.len = len;
  pm_sd_gateway::submit(&r);

  if (!r.ok) {
    ESP_LOGE(tag, "Append failed for %s (%u of %u bytes)", path, (unsigned)r.retVal, (unsigned)len);
  }
  return r.ok;
}

size_t PocketmageSD::getFileSize(const char *path) {
  if (noSD_)
    return 0;
  size_t size = 0;
  return statPath(path, &size, nullptr) ? size : 0;
}

// ===================== gateway-backed primitives =====================

bool PocketmageSD::exists(const char *path) {
  pm_sd_gateway::Request r{};
  r.op = pm_sd_gateway::Op::Stat;
  r.path = path;
  pm_sd_gateway::submit(&r);
  return r.ok;
}

bool PocketmageSD::statIsDir(const char *path) {
  size_t size = 0;
  bool isDir = false;
  return statPath(path, &size, &isDir) && isDir;
}

bool PocketmageSD::statPath(const char *path, size_t *sizeOut, bool *isDirOut) {
  pm_sd_gateway::Request r{};
  r.op = pm_sd_gateway::Op::Stat;
  r.path = path;
  r.isDirOut = isDirOut;
  pm_sd_gateway::submit(&r);
  if (r.ok && sizeOut)
    *sizeOut = r.retVal;
  return r.ok;
}

bool PocketmageSD::mkdirPath(const char *path) {
  pm_sd_gateway::Request r{};
  r.op = pm_sd_gateway::Op::Mkdir;
  r.path = path;
  pm_sd_gateway::submit(&r);
  return r.ok;
}

bool PocketmageSD::removeFile(const char *path) {
  pm_sd_gateway::Request r{};
  r.op = pm_sd_gateway::Op::Remove;
  r.path = path;
  pm_sd_gateway::submit(&r);
  return r.ok;
}

bool PocketmageSD::removeTree(const char *path) {
  pm_sd_gateway::Request r{};
  r.op = pm_sd_gateway::Op::RemoveTree;
  r.path = path;
  pm_sd_gateway::submit(&r);
  return r.ok;
}

bool PocketmageSD::renamePath(const char *path1, const char *path2) {
  pm_sd_gateway::Request r{};
  r.op = pm_sd_gateway::Op::Rename;
  r.path = path1;
  r.path2 = path2;
  pm_sd_gateway::submit(&r);
  return r.ok;
}

bool PocketmageSD::copyFileRaw(const char *src, const char *dst) {
  pm_sd_gateway::Request r{};
  r.op = pm_sd_gateway::Op::CopyFile;
  r.path = src;
  r.path2 = dst;
  pm_sd_gateway::submit(&r);
  return r.ok;
}

bool PocketmageSD::writeBinaryFile(const char *path, const uint8_t *buf, size_t len) {
  pm_sd_gateway::Request r{};
  r.op = pm_sd_gateway::Op::WriteBinary;
  r.path = path;
  r.wbuf = buf;
  r.len = len;
  pm_sd_gateway::submit(&r);
  return r.ok;
}

int PocketmageSD::streamOpen(const char *path, char mode) {
  pm_sd_gateway::Request r{};
  r.op = pm_sd_gateway::Op::StreamOpen;
  r.path = path;
  r.whence = mode;
  pm_sd_gateway::submit(&r);
  return r.ok ? (int)r.retVal : -1;
}

size_t PocketmageSD::streamRead(int slot, void *buf, size_t len) {
  pm_sd_gateway::Request r{};
  r.op = pm_sd_gateway::Op::StreamRead;
  r.slot = slot;
  r.rbuf = static_cast<uint8_t *>(buf);
  r.len = len;
  pm_sd_gateway::submit(&r);
  return r.retVal;
}

size_t PocketmageSD::streamWrite(int slot, const void *buf, size_t len) {
  pm_sd_gateway::Request r{};
  r.op = pm_sd_gateway::Op::StreamWrite;
  r.slot = slot;
  r.wbuf = static_cast<const uint8_t *>(buf);
  r.len = len;
  pm_sd_gateway::submit(&r);
  return r.retVal;
}

bool PocketmageSD::streamSeek(int slot, long offset, int whence) {
  pm_sd_gateway::Request r{};
  r.op = pm_sd_gateway::Op::StreamSeek;
  r.slot = slot;
  r.off = offset;
  r.whence = whence;
  pm_sd_gateway::submit(&r);
  return r.ok;
}

size_t PocketmageSD::streamTell(int slot) {
  pm_sd_gateway::Request r{};
  r.op = pm_sd_gateway::Op::StreamTell;
  r.slot = slot;
  pm_sd_gateway::submit(&r);
  return r.retVal;
}

size_t PocketmageSD::streamSize(int slot) {
  pm_sd_gateway::Request r{};
  r.op = pm_sd_gateway::Op::StreamSize;
  r.slot = slot;
  pm_sd_gateway::submit(&r);
  return r.retVal;
}

bool PocketmageSD::streamClose(int slot) {
  pm_sd_gateway::Request r{};
  r.op = pm_sd_gateway::Op::StreamClose;
  r.slot = slot;
  pm_sd_gateway::submit(&r);
  return r.ok;
}

int PocketmageSD::dirEnumOpen(const char *path) {
  pm_sd_gateway::Request r{};
  r.op = pm_sd_gateway::Op::DirEnum;
  r.path = path;
  pm_sd_gateway::submit(&r);
  return r.ok ? (int)r.retVal : -1;
}

bool PocketmageSD::dirEnumEntry(int i, char *name_out, size_t name_size, bool *is_dir) {
  return pm_sd_gateway::snapshotEntry(i, name_out, name_size, is_dir);
}

void PocketmageSD::dirEnumClose(void) {
  pm_sd_gateway::Request r{};
  r.op = pm_sd_gateway::Op::DirEnumClose;
  pm_sd_gateway::submit(&r);
}

bool PocketmageSD::extractTar(const char *tarPath, const char *destDir, volatile int *progress) {
  pm_sd_gateway::Request r{};
  r.op = pm_sd_gateway::Op::ExtractTar;
  r.path = tarPath;
  r.path2 = destDir;
  r.progress = progress;
  pm_sd_gateway::submit(&r);
  return r.ok;
}

bool PocketmageSD::listDirItems(const char *path, std::vector<DirItem> &out) {
  pm_sd_gateway::Request r{};
  r.op = pm_sd_gateway::Op::DirList;
  r.path = path;
  r.dirListOut = &out;
  pm_sd_gateway::submit(&r);
  return r.ok;
}
