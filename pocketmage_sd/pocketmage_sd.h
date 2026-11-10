//  .d88888b  888888ba   //
//  88.    "' 88    `8b  //
//  `Y88888b. 88     88  //
//        `8b 88     88  //
//  d8'   .8P 88    .8P  //
//   Y88888P  8888888P   //

#pragma once
#include <config.h>
#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

#include "driver/sdmmc_host.h"

// forward-declaration to avoid including U8g2lib.h, GxEPD2_BW.h, pocketmage_oled.h, and
// pocketmage_eink.h
class PocketmageOled;
class PocketmageEink;

class PocketmageSD {
public:
  explicit PocketmageSD() {
  }

  enum Mode { SDMMC = 0, SDSPI = 1 };

  void setMode(Mode m) {
    mode_ = m;
  }
  Mode getMode() const {
    return mode_;
  }

  void saveFile();
  void writeMetadata(const char *path);
  void loadFile(bool showOLED = true);
  void delFile(const char *fileName);
  void deleteMetadata(const char *path);
  void renFile(const char *oldFile, const char *newFile);
  void renMetadata(const char *oldPath, const char *newPath);
  void copyFile(const char *oldFile, const char *newFile);
  void appendToFile(const char *path, const char *inText);

  void beginIO();
  void endIO();

  // Getters / Setters
  bool getNoSD() const {
    return noSD_;
  }
  void setNoSD(bool v) {
    noSD_ = v;
  }

  String getWorkingFile() const {
    return workingFile_;
  }
  void setWorkingFile(const String &v) {
    workingFile_ = v;
  }

  String getEditingFile() const {
    return editingFile_;
  }
  void setEditingFile(const String &v) {
    editingFile_ = v;
  }

  String getFilesListIndex(int index) const {
    return filesList_[index];
  }
  void setFilesListIndex(int index, const String &v) {
    filesList_[index] = v;
  }

  // Native ESP-IDF SD operations (paths are relative, mount point is prepended internally)
  void listDir(const char *dirname);
  void readFile(const char *path);
  String readFileToString(const char *path);
  void writeFile(const char *path, const char *message);
  void appendFile(const char *path, const char *message);
  void renameFile(const char *path1, const char *path2);
  void deleteFile(const char *path);
  bool readBinaryFile(const char *path, uint8_t *buf, size_t len);
  // Append raw bytes to a file (creates if missing). No String/Text overhead:
  // callers stream chunks without holding the whole file in RAM.
  bool appendBinaryFile(const char *path, const uint8_t *buf, size_t len);
  size_t getFileSize(const char *path);

  // Gateway-backed primitives. All card I/O goes through the SD gateway task;
  // these are the only sanctioned direct operations (whole-file, stream, and
  // tar). Paths are relative; already-prefixed paths pass through unchanged.
  bool exists(const char *path);    // stat probe
  bool statIsDir(const char *path); // true when path is a directory
  // Single stat: exists + size + is-directory. Paths failing to stat return
  // false with the outputs left untouched.
  bool statPath(const char *path, size_t *sizeOut, bool *isDirOut);
  bool mkdirPath(const char *path); // true when it exists afterwards
  bool removeFile(const char *path);
  bool removeTree(const char *path); // recursive delete
  bool renamePath(const char *path1, const char *path2);
  bool copyFileRaw(const char *src, const char *dst); // binary, 4KB chunks
  /** Replace path with len bytes from buf; return false on write/close failure.
   * Null/empty paths and null buffers with nonzero len are rejected.
   */
  bool writeBinaryFile(const char *path, const uint8_t *buf, size_t len);

  // Handle-based streaming for decoders and loaders. Slots come from
  // streamOpen and must be released with streamClose. These are the only
  // disk-streaming entry points in the OS; nothing else reads files
  // incrementally.
  /** Open path in mode 'r', 'w', or 'a'; return a slot or -1 on failure.
   * An open stream inhibits idle sleep and CPU-frequency changes until closed.
   */
  int streamOpen(const char *path, char mode);
  /** Read up to len bytes into buf; return bytes read, or zero on EOF/error. */
  size_t streamRead(int slot, void *buf, size_t len);
  /** Write len bytes from buf; return bytes accepted (possibly short on error). */
  size_t streamWrite(int slot, const void *buf, size_t len);
  /** Seek offset relative to SEEK_SET, SEEK_CUR, or SEEK_END; false on failure. */
  bool streamSeek(int slot, long offset, int whence);
  /** Return the byte position, or zero on failure. */
  size_t streamTell(int slot);
  /** Return file size while restoring the position, or zero on failure. */
  size_t streamSize(int slot);
  /** Release slot and return whether close/flush succeeded; false for invalid slots. */
  bool streamClose(int slot);

  // Directory enumeration backed by a gateway snapshot. One enumeration is
  // live at a time; a new open discards the previous one.
  int dirEnumOpen(const char *path); // entry count, or -1
  bool dirEnumEntry(int i, char *name_out, size_t name_size, bool *is_dir);
  void dirEnumClose(void);

  // One entry from a full directory listing.
  struct DirItem {
    String name;
    bool isDir;
  };

  // Full listing (files and directories, no filtering) into a caller-owned
  // vector. Single gateway op, so recursive listings are safe.
  bool listDirItems(const char *path, std::vector<DirItem> &out);

  // Extract a ustar archive from the card into destDir. Entry names are
  // sanitized (no "..", no absolute paths). progress, when set, receives
  // 0..100 as archive bytes are consumed; volatile so a UI task can watch
  // it while the installer blocks inside the call.
  bool extractTar(const char *tarPath, const char *destDir, volatile int *progress);

  static constexpr const char *kSdMountPoint = "/sdcard";
  static constexpr size_t kMaxPathLen = 256;

  // Resolve a relative path (e.g. "/music") to a full VFS path (e.g. "/sdcard/music")
  static const char *sdPath(const char *relPath);
  static bool sdExists(const char *path);
  static bool sdMkdir(const char *path);
  static bool sdRemove(const char *path);
  static bool sdRename(const char *path1, const char *path2);
  static bool sdGetFileInfo(const char *path, size_t *size);

private:
  Mode mode_ = SDMMC;
  bool noSD_ = false;
  bool SDActive = false;

  String editingFile_;
  String workingFile_;
  String filesList_[MAX_FILES];

  static constexpr const char *tag = "SD";
  int fileIndex_ = 0;
  std::vector<String> excludedFiles_;
};

// Global singleton accessor
PocketmageSD &PM_SD();
PocketmageSD &PM_SDAUTO();

// Mount the SD card via SDMMC
bool mountSdmmc();
sdmmc_card_t *getSdCard();
