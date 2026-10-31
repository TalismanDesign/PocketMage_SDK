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

Standard file operations that take an explicit `fs::FS`. Apps storing their assets read them from `/assets/<name>` (see [publishing](../guides/publishing.md)) with `readFileToString` / `readBinaryFile`; `readBinaryFile` returns false on a partial read.

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
```

## C ABI (`pm_*`)

External ELF apps call these C wrappers rather than the C++ class above; they are generated from it by `gen_app_api.py` and declared in `pm_sdk_app.h` (pulled in via `pm_app_api.h`). See the [function index](functions.md) and the [projection model](index.md#api-reference-the-c-abi-pm) for the general rules.

```c
int pm_sd_get_mode();
void pm_sd_save_file();
void pm_sd_write_metadata(const char* a0);
void pm_sd_load_file(bool a0);
void pm_sd_del_file(const char* a0);
void pm_sd_delete_metadata(const char* a0);
void pm_sd_ren_file(const char* a0, const char* a1);
void pm_sd_ren_metadata(const char* a0, const char* a1);
void pm_sd_copy_file(const char* a0, const char* a1);
void pm_sd_append_to_file(const char* a0, const char* a1);
void pm_sd_begin_io();
void pm_sd_end_io();
bool pm_sd_get_no_sd();
const char* pm_sd_get_working_file();
const char* pm_sd_get_editing_file();
const char* pm_sd_get_files_list_index(int a0);
void pm_sd_list_dir(const char* a1);
void pm_sd_read_file(const char* a1);
const char* pm_sd_read_file_to_string(const char* a1);
void pm_sd_write_file(const char* a1, const char* a2);
void pm_sd_append_file(const char* a1, const char* a2);
void pm_sd_rename_file(const char* a1, const char* a2);
void pm_sd_delete_file(const char* a1);
bool pm_sd_read_binary_file(const char* a0, uint8_t* a1, size_t a2);
size_t pm_sd_get_file_size(const char* a0);
```

All operate on the `PM_SDAUTO()` singleton. Every `const char*` return points into the rotating string pool; copy it before the next SDK call if it must survive four further string-returning calls.

- `pm_sd_get_mode()` is `getMode()`. Returns the `Mode` value (`SDMMC` = 0, `SDSPI` = 1) as int.
- `pm_sd_save_file()` is `saveFile()`.
- `pm_sd_write_metadata(a0)` is `writeMetadata(path)`.
- `pm_sd_load_file(a0)` is `loadFile(showOLED)`.
- `pm_sd_del_file(a0)` is `delFile(fileName)`.
- `pm_sd_delete_metadata(a0)` is `deleteMetadata(path)`.
- `pm_sd_ren_file(a0, a1)` is `renFile(oldFile, newFile)`.
- `pm_sd_ren_metadata(a0, a1)` is `renMetadata(oldPath, newPath)`.
- `pm_sd_copy_file(a0, a1)` is `copyFile(oldFile, newFile)`.
- `pm_sd_append_to_file(a0, a1)` is `appendToFile(path, inText)`.
- `pm_sd_begin_io()` / `pm_sd_end_io()` are `beginIO()` / `endIO()`; they toggle the `SDActive` indicator around longer operations.
- `pm_sd_get_no_sd()` is `getNoSD()`. Returns true when no card was found.
- `pm_sd_get_working_file()` / `pm_sd_get_editing_file()` are `getWorkingFile()` / `getEditingFile()`.
- `pm_sd_get_files_list_index(a0)` is `getFilesListIndex(index)`; `a0` is the slot 0..`MAX_FILES`-1.
- `pm_sd_list_dir(a1)` is `listDir(fs, dirname)`; the `fs::FS&` first argument is dropped from the C ABI (the active filesystem is used), so `a1` is the directory path.
- `pm_sd_read_file(a1)` is `readFile(fs, path)`; `a1` is the path, with the `fs::FS&` dropped.
- `pm_sd_read_file_to_string(a1)` is `readFileToString(fs, path)`; `a1` is the path, with the `fs::FS&` dropped. Returns a pointer into the rotating string pool.
- `pm_sd_write_file(a1, a2)` is `writeFile(fs, path, message)`; `a1` is the path, `a2` is the content, with the `fs::FS&` dropped.
- `pm_sd_append_file(a1, a2)` is `appendFile(fs, path, message)`; `a1`/`a2` as above, with the `fs::FS&` dropped.
- `pm_sd_rename_file(a1, a2)` is `renameFile(fs, path1, path2)`; `a1`/`a2` are the paths, with the `fs::FS&` dropped.
- `pm_sd_delete_file(a1)` is `deleteFile(fs, path)`; `a1` is the path, with the `fs::FS&` dropped.
- `pm_sd_read_binary_file(a0, a1, a2)` is `readBinaryFile(path, buf, len)`; `a0` is the path, `a1` is the caller buffer, `a2` is its size. Returns false on a partial read.
- `pm_sd_get_file_size(a0)` is `getFileSize(path)`. Returns the byte size.
