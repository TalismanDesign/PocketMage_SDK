//  .d88888b  888888ba   //
//  88.    "' 88    `8b  //
//  `Y88888b. 88     88  //
//        `8b 88     88  //
//  d8'   .8P 88    .8P  //
//   Y88888P  8888888P   //

#pragma once

#include <cstddef>
#include <cstdint>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

class String;

namespace pm_sd_gateway {

enum class Op : uint8_t {
  Stat,           // path -> ok = exists, retVal = size, isDirOut = S_ISDIR
  Mkdir,          // path -> ok = true if the path exists afterwards
  Remove,         // path -> ok = remove() == 0
  RemoveTree,     // path -> ok = recursive delete (dirs and files)
  Rename,         // path, path2 -> ok = rename() == 0
  CopyFile,       // path, path2 -> ok = binary copy, 4KB chunks
  ListDir,        // path -> strOut[cap], countOut; excluded filter, files only
  ReadFileString, // path -> *strOut = whole file content
  WriteFile,      // path, text -> ok = opened "w" and fputs succeeded
  AppendFile,     // path, text -> ok = opened "a", fputs + trailing newline
  ReadBinary,     // path, rbuf, len -> ok = full read
  WriteBinary,    // path, wbuf, len -> ok = full write ("w", truncating)
  AppendBinary,   // path, wbuf, len -> ok = full write ("a")
  DirEnum,        // path -> gateway-owned snapshot, retVal = count; -1 on error
  DirEnumClose,   // free the snapshot held by a prior DirEnum
  DirList,        // path -> dirListOut vector filled with all entries
  ExtractTar,     // path = tar, path2 = dest dir; progress ptr, 0..100
  StreamOpen,     // path, whence = mode char ('r','w','a') -> retVal = slot id
  StreamRead,     // slot, rbuf, len -> retVal = bytes read (with retries)
  StreamWrite,    // slot, wbuf, len -> retVal = bytes written
  StreamSeek,     // slot, off, whence (SEEK_*) -> ok
  StreamTell,     // slot -> retVal = position
  StreamSize,     // slot -> retVal = file size
  StreamClose,    // slot -> ok; frees the handle slot
  BeginUsbAccess, // drain prior work, unmount FAT, retain the card for raw MSC I/O
  CloseCallerResources, // close streams and snapshot opened by the submitting task
  DirEnumCount,   // retVal = this task's snapshot count
  DirEnumEntry,   // slot = index, nameOut/len = name output, isDirOut = entry type
};

struct Request {
  Op op;
  const char *path = nullptr;    // relative or already /sdcard-prefixed
  const char *path2 = nullptr;   // rename/copy destination, tar destination dir
  const char *text = nullptr;    // WriteFile / AppendFile payload
  const uint8_t *wbuf = nullptr; // binary write payload
  uint8_t *rbuf = nullptr;       // read target buffer
  char *nameOut = nullptr;      // DirEnumEntry name output buffer
  size_t len = 0;
  long off = 0;                     // StreamSeek offset
  int whence = 0;                   // StreamSeek SEEK_* or StreamOpen mode char
  int slot = -1;                    // stream handle from StreamOpen
  String *strOut = nullptr;         // ReadFileString / ListDir output array
  int cap = 0;                      // ListDir array capacity
  int *countOut = nullptr;          // ListDir written entry count
  const void *excluded = nullptr;   // const std::vector<String>* filter
  bool *isDirOut = nullptr;         // Stat: S_ISDIR
  volatile int *progress = nullptr; // ExtractTar: 0..100 while extracting
  void *dirListOut = nullptr;
  TaskHandle_t caller = nullptr; // set by submit(); the gateway notifies it
  bool ok = false;
  size_t retVal = 0;
};

void start();

bool submit(Request *r);

/** Return this task's snapshot count through the gateway, or zero if unavailable. */
int snapshotCount();
/** Copy entry i into name_out (name_size includes NUL) and optional is_dir.
 * Serialized with replacement/close; false leaves outputs unchanged on invalid
 * input, a foreign snapshot, or gateway failure.
 */
bool snapshotEntry(int i, char *name_out, size_t name_size, bool *is_dir);
bool streamsOpen();

/** Unmount FAT after queued I/O completes and reserve the card until reboot.
 * Return false if files remain open, no card is mounted, or unmount fails.
 * The card and host remain owned by the SD service; raw clients must not free them.
 */
bool beginUsbAccess();
/** Return whether USB owns the card and filesystem requests are blocked. */
bool usbAccessActive();

/** Close this task's streams and directory snapshot through the serialized gateway.
 * Return false if any stream failed to flush; all matching handles are still released.
 */
bool closeCallerResources();

} // namespace pm_sd_gateway
