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
};

struct Request {
  Op op;
  const char *path = nullptr;    // relative or already /sdcard-prefixed
  const char *path2 = nullptr;   // rename/copy destination, tar destination dir
  const char *text = nullptr;    // WriteFile / AppendFile payload
  const uint8_t *wbuf = nullptr; // binary write payload
  uint8_t *rbuf = nullptr;       // read target buffer
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

int snapshotCount();
bool snapshotEntry(int i, char *name_out, size_t name_size, bool *is_dir);
void snapshotFree();
bool streamsOpen();

} // namespace pm_sd_gateway
