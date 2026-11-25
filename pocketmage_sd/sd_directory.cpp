#include "sd_directory.h"

#include "pocketmage_sd.h"

#include <atomic>
#include <cerrno>
#include <climits>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <dirent.h>
#include <sys/stat.h>

namespace pm_sd_gateway {
namespace {

constexpr int kDirectorySlots = 4;
struct Cursor {
  DIR *directory;
  TaskHandle_t owner;
  int handle;
  struct dirent *pending;
  bool isDirectory;
  bool exhausted;
  bool failed;
  char path[PocketmageSD::kMaxPathLen];
};

Cursor *cursors[kDirectorySlots]{};
std::atomic<int> openCount{0};
uint32_t nextHandle = 1;

Cursor *findCursor(int handle) {
  for (Cursor *cursor : cursors) {
    if (cursor && cursor->handle == handle)
      return cursor;
  }
  return nullptr;
}

bool fillPending(Cursor *cursor) {
  if (cursor->failed)
    return false;
  while (!cursor->pending && !cursor->exhausted) {
    // POSIX distinguishes EOF from read failure through errno.
    // https://pubs.opengroup.org/onlinepubs/9799919799/functions/readdir.html
    errno = 0;
    struct dirent *entry = readdir(cursor->directory);
    if (!entry) {
      cursor->failed = errno != 0;
      cursor->exhausted = !cursor->failed;
      return !cursor->failed;
    }
    if (strcmp(entry->d_name, ".") == 0 || strcmp(entry->d_name, "..") == 0 ||
        strcmp(entry->d_name, ".DS_Store") == 0 || strncmp(entry->d_name, "._", 2) == 0)
      continue;
    cursor->pending = entry;
    cursor->isDirectory = entry->d_type == DT_DIR;
    if (entry->d_type == DT_UNKNOWN) {
      char child[PocketmageSD::kMaxPathLen];
      const int length = snprintf(child, sizeof(child), "%s/%s", cursor->path, entry->d_name);
      struct stat info;
      if (length < 0 || static_cast<size_t>(length) >= sizeof(child) || stat(child, &info) != 0) {
        cursor->failed = true;
        return false;
      }
      cursor->isDirectory = S_ISDIR(info.st_mode);
    }
  }
  return true;
}

} // namespace

void directoryOpen(Request *request) {
  request->ok = false;
  if (!request->caller || !request->path || !request->path[0] || nextHandle > INT_MAX)
    return;
  int slot = -1;
  for (int i = 0; i < kDirectorySlots; ++i) {
    if (!cursors[i]) {
      slot = i;
      break;
    }
  }
  if (slot < 0)
    return;
  Cursor *cursor = static_cast<Cursor *>(std::malloc(sizeof(Cursor)));
  if (!cursor)
    return;
  *cursor = Cursor{};
  const char *mount = PocketmageSD::kSdMountPoint;
  const size_t mountLength = strlen(mount);
  const bool prefixed =
      strncmp(request->path, mount, mountLength) == 0 &&
      (mountLength == 1 || request->path[mountLength] == '/' || request->path[mountLength] == '\0');
  const int length = prefixed ? snprintf(cursor->path, sizeof(cursor->path), "%s", request->path)
                              : snprintf(cursor->path, sizeof(cursor->path), "%s%s%s", mount,
                                         request->path[0] == '/' ? "" : "/", request->path);
  if (length < 0 || static_cast<size_t>(length) >= sizeof(cursor->path)) {
    std::free(cursor);
    return;
  }
  cursor->directory = opendir(cursor->path);
  if (!cursor->directory) {
    std::free(cursor);
    return;
  }
  // Handles never wrap or recycle, including after cleanup/remount. Exhaustion rejects opens.
  cursor->handle = static_cast<int>(nextHandle++);
  cursor->owner = request->caller;
  cursors[slot] = cursor;
  ++openCount;
  request->retVal = static_cast<size_t>(cursor->handle);
  request->ok = true;
}

void directoryRead(Request *request) {
  request->ok = false;
  Cursor *cursor = findCursor(request->slot);
  if (!cursor || !directoryOwned(request->slot, request->caller) ||
      (!request->nameOut && request->len != 0) || !fillPending(cursor))
    return;
  if (!cursor->pending) {
    request->retVal = 0;
    request->ok = true;
    return;
  }
  const size_t required = strlen(cursor->pending->d_name) + 1;
  if (request->nameOut) {
    if (request->len < required)
      return;
    memcpy(request->nameOut, cursor->pending->d_name, required);
  }
  if (request->isDirOut)
    *request->isDirOut = cursor->isDirectory;
  request->retVal = required;
  request->ok = true;
  if (request->nameOut)
    cursor->pending = nullptr;
}

bool directoryOwned(int handle, TaskHandle_t owner) {
  const Cursor *cursor = findCursor(handle);
  return owner && cursor && cursor->owner == owner;
}

bool directoryClose(int handle) {
  for (Cursor *&cursor : cursors) {
    if (!cursor || cursor->handle != handle)
      continue;
    // ESP VFS consumes its directory wrapper on close, including a failed filesystem close.
    // https://docs.espressif.com/projects/esp-idf/en/release-v5.5/esp32s3/api-reference/storage/vfs.html
    const bool closed = closedir(cursor->directory) == 0;
    std::free(cursor);
    cursor = nullptr;
    --openCount;
    return closed;
  }
  return false;
}

bool directoryCloseOwner(TaskHandle_t owner) {
  if (!owner)
    return false;
  bool ok = true;
  for (Cursor *cursor : cursors) {
    if (cursor && cursor->owner == owner && !directoryClose(cursor->handle))
      ok = false;
  }
  return ok;
}

bool directoryCloseAll() {
  bool ok = true;
  for (Cursor *cursor : cursors) {
    if (cursor && !directoryClose(cursor->handle))
      ok = false;
  }
  return ok;
}

bool directoriesOpen() {
  return openCount > 0;
}

} // namespace pm_sd_gateway
