---
type: api
title: "SD card (pocketmage_sd)"
description: "Filesystems, files, metadata, and the dual SDMMC/SDSPI mode switch."
source: "https://talismandesign.github.io/PocketMage_SDK/docs/api/sd/"
path: /api/sd/
updated: 2026-10-05
okf:
  generated_by: "@docmd/plugin-okf"
  generated_at: "2026-10-05T01:48:20.783Z"
---
---
title: "SD card (pocketmage_sd)"
description: "Filesystems, files, metadata, and the dual SDMMC/SDSPI mode switch."
---

# SD card API

The SD card is the device's persistent storage: the OS keeps its files, metadata, app slots, and the app data sandbox there. `PocketmageSD` abstracts the two bus modes, the file bookkeeping, and the per-file metadata format, and exposes raw FS helpers for arbitrary paths.

## Access

```cpp
#include <pocketmage.h>

PocketmageSD& sd = PM_SDAUTO();   // alias of PM_SD(); both names exist
```

`PM_SD()` and `PM_SDAUTO()` return the same single instance; `PM_SDAUTO` is the name the host exports to loaded apps (see [symbols](../symbols.md)). `setupSD()` runs in `PocketMage_INIT()`.

## Mode switch

```cpp
enum Mode { SDMMC = 0, SDSPI = 1 };
void setMode(Mode m);
Mode getMode() const;
```

`setupSD()` tries 4-bit SDMMC (pins `SD_CLK/CMD/D0..D3`) at boot and, if that fails, falls back to SPI mode (`SD_CS/MOSI/SCK/MISO`) on the shared SPI bus. The mode is visible through `getMode()`; `SD_SPI_COMPATIBILITY` persists the last known-good mode. `beginIO()` / `endIO()` toggle the `SDActive` status-line indicator around longer operations and can be called per file operation.

## File bookkeeping

The class tracks the OS's working set:

```cpp
void saveFile();                    // writes workingFile_ (temp -> real)
void writeMetadata(const String& path);
void loadFile(bool showOLED = true);
void delFile(String fileName);
void deleteMetadata(String path);
void renFile(String oldFile, String newFile);
void renMetadata(String oldPath, String newPath);
void copyFile(String oldFile, String newFile);
void appendToFile(String path, String inText);
```

- `loadFile` opens the editing file into the in-memory buffer, optionally flashing the OLED.
- Metadata files record a timestamp and a byte count (see `countVisibleCharsFile`), updated by `saveFile`/`writeMetadata` and removed by the matching `delete*`/`ren*`. The metadata path is derived from the file path.
- `saveFile` moves the temp working file to the final name, updates metadata, and refreshes the file list slot. All operations toggle `SDActive` and honor the active mode's FS instance.

## Getters / setters

```cpp
bool    getNoSD() const;         // true when no card was found
void    setNoSD(bool v);
String  getWorkingFile() const;
void    setWorkingFile(const String& v);
String  getEditingFile() const;
void    setEditingFile(const String& v);
String  getFilesListIndex(int index) const;
void    setFilesListIndex(int index, const String& v);
```

`MAX_FILES` (10) file slots back the file list used by the picker. The `excludedFiles_` set (`/temp.txt`, `/settings.txt`, `/tasks.txt`) never appears in listings.

## Raw FS helpers

```cpp
void    listDir(fs::FS& fs, const char* dirname);
void    readFile(fs::FS& fs, const char* path);
String  readFileToString(fs::FS& fs, const char* path);
void    writeFile(fs::FS& fs, const char* path, const char* message);
void    appendFile(fs::FS& fs, const char* path, const char* message);
void    renameFile(fs::FS& fs, const char* path1, const char* path2);
void    deleteFile(fs::FS& fs, const char* path);
bool    readBinaryFile(const char* path, uint8_t* buf, size_t len);
size_t  getFileSize(const char* path);
```

Standard file operations that take an explicit `fs::FS`. Apps storing their assets read them from `/assets/<name>` (see [publishing](../publish.md)) with `readFileToString` / `readBinaryFile`; `readBinaryFile` returns false on a partial read.

## Example

```cpp
#include <pocketmage.h>

extern "C" int main(int, char**) {
  if (PM_SDAUTO().getNoSD()) return 0;
  PM_SDAUTO().appendToFile("/assets/home/notes.txt", "remember: milk\n");
  String text = PM_SDAUTO().readFileToString(*global_fs, "/assets/home/notes.txt");
  // draw text...
  return 0;
}
