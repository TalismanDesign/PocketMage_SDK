//  .d88888b  888888ba   //
//  88.    "' 88    `8b  //
//  `Y88888b. 88     88  //
//        `8b 88     88  //
//  d8'   .8P 88    .8P  //
//   Y88888P  8888888P   //

#include "sd_gateway.h"
#include "sd_completion.h"
#include "sd_directory.h"

#include <config.h>
#include <pocketmage.h>
#include <pocketmage_globals.h>

#include <cstdio>
#include <atomic>
#include <cstdlib>
#include <cstring>
#include <cerrno>
#include <dirent.h>
#include <mutex>
#include <sys/stat.h>
#include <vector>

#include <Arduino.h>
#include <microtar.h>
#include "driver/sdmmc_host.h"
#include "esp_vfs_fat.h"
#include "ff.h"
#include "diskio_sdmmc.h"

#include "pocketmage_sd.h"

extern bool SAVE_POWER;
extern bool noTimeout;

// macOS resource forks (._*) and .DS_Store
static bool isMacMeta(const char *name) {
  if (!name || !name[0])
    return false;
  if (strcmp(name, ".DS_Store") == 0)
    return true;
  if (name[0] == '.' && name[1] == '_')
    return true;
  return false;
}

namespace pm_sd_gateway {

static constexpr const char *TAG = "SD";

static constexpr size_t kCopyChunk = 4 * 1024;
static constexpr int kReadRetries = 3;
static constexpr size_t kDirNameLen = 128;

static_assert(CONFIG_FREERTOS_TASK_NOTIFICATION_ARRAY_ENTRIES > kCompletionSlot,
              "SD completion needs a task notification slot separate from logger wakeups");

struct SnapEntry {
  char name[kDirNameLen];
  bool is_dir;
};

static QueueHandle_t s_queue = nullptr;
static TaskHandle_t s_task = nullptr;
static FILE *s_streams[kStreamSlots] = {};
static TaskHandle_t s_streamOwners[kStreamSlots] = {};
static TaskHandle_t s_snapshotOwner = nullptr;
static SnapEntry *s_snapshot = nullptr;
static int s_snapshotCount = 0;

static std::atomic<int> s_openStreams{0};
static std::atomic<bool> s_usbAccess{false};
static bool s_needRemount = false;
// Also serialize the inline fallback if the gateway task could not be created.
// https://docs.espressif.com/projects/esp-idf/en/release-v5.5/esp32s3/api-reference/system/pthread.html
static std::recursive_mutex s_executionMutex;

static const char *resolve(const char *path, char *buf, size_t size) {
  if (!path)
    return nullptr;
  constexpr const char *kMount = PocketmageSD::kSdMountPoint;
  if (strncmp(path, kMount, strlen(kMount)) == 0)
    return path;
  snprintf(buf, size, "%s%s", kMount, path);
  return buf;
}

static void exec(Request *r);
static void remount();

/** FreeRTOS adapter for the completion slot. */
class GatewayNotify {
 public:
  explicit GatewayNotify(TaskHandle_t task) : task_(task) {}

  void clear(unsigned slot) { ulTaskNotifyTakeIndexed(slot, pdTRUE, 0); }
  void take(unsigned slot) { ulTaskNotifyTakeIndexed(slot, pdTRUE, portMAX_DELAY); }
  void give(unsigned slot) { xTaskNotifyGiveIndexed(task_, slot); }

 private:
  TaskHandle_t task_;
};

static void taskFunc(void *) {
  for (;;) {
    Request *r = nullptr;
    if (xQueueReceive(s_queue, &r, 0) != pdPASS) {
      if (!streamsOpen() && SAVE_POWER && getCpuFrequencyMhz() != POWER_SAVE_FREQ)
        pocketmage::setCpuSpeed(POWER_SAVE_FREQ);
      xQueueReceive(s_queue, &r, portMAX_DELAY);
    }
    if (!r)
      continue;

    pocketmage::setCpuSpeed(240);

    bool prevTimeout = noTimeout;
    noTimeout = true;
    exec(r);
    noTimeout = prevTimeout;

    GatewayNotify notify(r->caller);
    CompletionWait<GatewayNotify> completion(notify);
    completion.signal();
  }
}

void start() {
  if (s_queue)
    return;
  s_queue = xQueueCreate(8, sizeof(Request *));
  if (!s_queue) {
    PM_LOGE(TAG, "gateway queue create failed; SD ops stay inline");
    return;
  }
  if (xTaskCreatePinnedToCore(taskFunc, "pm_sd", 6144, nullptr, 2, &s_task, 1) != pdPASS) {
    PM_LOGE(TAG, "gateway task create failed; SD ops stay inline");
    vQueueDelete(s_queue);
    s_queue = nullptr;
  }
}

bool submit(Request *r) {
  const TaskHandle_t caller = xTaskGetCurrentTaskHandle();
  r->caller = caller;
  if (!s_queue || !caller || caller == s_task) {
    exec(r);
    return r->ok;
  }
  GatewayNotify notify(caller);
  CompletionWait<GatewayNotify> completion(notify);
  completion.reset();
  if (xQueueSend(s_queue, &r, portMAX_DELAY) != pdPASS)
    return false;
  completion.wait();
  return r->ok;
}

int snapshotCount() {
  Request request{};
  request.op = Op::DirEnumCount;
  return submit(&request) ? static_cast<int>(request.retVal) : 0;
}

bool snapshotEntry(int i, char *name_out, size_t name_size, bool *is_dir) {
  Request request{};
  request.op = Op::DirEnumEntry;
  request.slot = i;
  request.nameOut = name_out;
  request.len = name_size;
  request.isDirOut = is_dir;
  return submit(&request);
}

static bool copySnapshotEntry(Request *r) {
  if (!s_snapshot || r->slot < 0 || r->slot >= s_snapshotCount || !r->nameOut || r->len == 0)
    return false;
  const SnapEntry &e = s_snapshot[r->slot];
  strncpy(r->nameOut, e.name, r->len - 1);
  r->nameOut[r->len - 1] = '\0';
  if (r->isDirOut)
    *r->isDirOut = e.is_dir;
  return true;
}

static void snapshotFree() {
  free(s_snapshot);
  s_snapshot = nullptr;
  s_snapshotCount = 0;
  s_snapshotOwner = nullptr;
}

static bool canAccessResources(const Request *r) {
  switch (r->op) {
  case Op::StreamRead:
  case Op::StreamWrite:
  case Op::StreamSeek:
  case Op::StreamTell:
  case Op::StreamSize:
  case Op::StreamClose:
    return r->caller && r->slot >= 0 && r->slot < kStreamSlots &&
           s_streams[r->slot] && s_streamOwners[r->slot] == r->caller;
  case Op::DirEnum:
  case Op::DirEnumCount:
  case Op::DirEnumEntry:
  case Op::DirEnumClose:
    return r->caller && (!s_snapshotOwner || s_snapshotOwner == r->caller);
  case Op::StreamOpen:
  case Op::DirIteratorOpen:
    return r->caller != nullptr;
  case Op::DirIteratorRead:
  case Op::DirIteratorClose:
    return directoryOwned(r->slot, r->caller);
  default:
    return true;
  }
}

static bool closeStream(int slot) {
  if (slot < 0 || slot >= kStreamSlots || !s_streams[slot])
    return false;
  const bool closed = fclose(s_streams[slot]) == 0;
  // fclose invalidates the handle even if flushing fails.
  s_streams[slot] = nullptr;
  s_streamOwners[slot] = nullptr;
  --s_openStreams;
  return closed;
}

static bool closeOwnedResources(TaskHandle_t owner) {
  if (!owner)
    return false;
  bool ok = directoryCloseOwner(owner);
  for (int slot = 0; slot < kStreamSlots; ++slot) {
    if (s_streams[slot] && s_streamOwners[slot] == owner && !closeStream(slot))
      ok = false;
  }
  if (s_snapshotOwner == owner)
    snapshotFree();
  return ok;
}

bool closeCallerResources() {
  Request request{};
  request.op = Op::CloseCallerResources;
  return submit(&request);
}

bool streamsOpen() {
  return s_openStreams > 0 || directoriesOpen();
}

bool beginUsbAccess() {
  // USB handoff requires the queue's serialization against other tasks.
  if (!s_queue)
    return false;
  Request request{};
  request.op = Op::BeginUsbAccess;
  return submit(&request);
}

bool usbAccessActive() {
  return s_usbAccess;
}

bool onGatewayTask() {
  return s_task && xTaskGetCurrentTaskHandle() == s_task;
}

static bool unmountForUsb() {
  if (s_usbAccess)
    return true;
  if (streamsOpen() || !getSdCard())
    return false;
  const BYTE drive = ff_diskio_get_pdrv_card(getSdCard());
  if (drive >= FF_VOLUMES || drive > 9)
    return false;
  const char path[] = {static_cast<char>('0' + drive), ':', '\0'};
  // f_mount(nullptr) unregisters FAT without destroying the SDMMC/SDSPI host.
  // No filesystem operation may follow host writes; USB exits through reboot.
  // http://elm-chan.org/fsw/ff/doc/mount.html
  const FRESULT result = f_mount(nullptr, path, 0);
  if (result != FR_OK) {
    PM_LOGE(TAG, "USB FAT unmount failed: %d", static_cast<int>(result));
    return false;
  }
  s_needRemount = false;
  s_usbAccess = true;
  return true;
}

static FILE *openResolved(const char *path, const char *mode) {
  if (!path)
    return nullptr;
  char full[PocketmageSD::kMaxPathLen];
  return fopen(resolve(path, full, sizeof(full)), mode);
}

static size_t streamFill(FILE *f, uint8_t *buf, size_t len) {
  size_t n = fread(buf, 1, len, f);
  int retries = 0;
  while (n < len && retries < kReadRetries) {
    if (n > 0 && feof(f))
      break;
    delay(10);
    size_t more = fread(buf + n, 1, len - n, f);
    if (more == 0 && (feof(f) || retries == kReadRetries - 1))
      break;
    n += more;
    retries++;
  }
  return n;
}

static bool removeTreeInner(const char *path) {
  struct stat st;
  if (stat(path, &st) != 0)
    return true;
  if (!S_ISDIR(st.st_mode))
    return remove(path) == 0;

  DIR *dir = opendir(path);
  if (!dir)
    return false;

  bool ok = true;
  struct dirent *entry;
  while ((entry = readdir(dir)) != nullptr) {
    if (strcmp(entry->d_name, ".") == 0 || strcmp(entry->d_name, "..") == 0)
      continue;
    char child[PocketmageSD::kMaxPathLen];
    snprintf(child, sizeof(child), "%s/%s", path, entry->d_name);
    if (entry->d_type == DT_DIR) {
      if (!removeTreeInner(child))
        ok = false;
    } else {
      if (remove(child) != 0)
        ok = false;
    }
  }
  closedir(dir);
  return ok && rmdir(path) == 0;
}

static bool copyFileInner(const char *src, const char *dst) {
  FILE *in = fopen(src, "r");
  if (!in) {
    PM_LOGE(TAG, "copy: open src failed: %s", src);
    return false;
  }
  FILE *out = fopen(dst, "w");
  if (!out) {
    PM_LOGE(TAG, "copy: open dst failed: %s", dst);
    fclose(in);
    return false;
  }

  uint8_t *buf = (uint8_t *)malloc(kCopyChunk);
  if (!buf) {
    fclose(in);
    fclose(out);
    return false;
  }

  bool ok = true;
  size_t n;
  while ((n = fread(buf, 1, kCopyChunk, in)) > 0) {
    if (fwrite(buf, 1, n, out) != n) {
      ok = false;
      break;
    }
  }
  free(buf);
  fclose(in);
  fclose(out);
  return ok;
}

static void listDirInner(Request *r) {
  char full[PocketmageSD::kMaxPathLen];
  const char *p = resolve(r->path, full, sizeof(full));
  if (!p) {
    PM_LOGE(TAG, "Failed to open directory: %s", r->path ? r->path : "(null)");
    *r->countOut = 0;
    r->ok = false;
    return;
  }

  DIR *dir = opendir(p);
  if (!dir) {
    PM_LOGE(TAG, "Failed to open directory: %s", full);
    *r->countOut = 0;
    r->ok = false;
    return;
  }

  const std::vector<String> *excluded = static_cast<const std::vector<String> *>(r->excluded);

  int count = 0;
  struct dirent *entry;
  while ((entry = readdir(dir)) != nullptr && count < r->cap) {
    if (entry->d_type == DT_DIR)
      continue;
    if (strcmp(entry->d_name, ".") == 0 || strcmp(entry->d_name, "..") == 0)
      continue;

    String fileName = String(entry->d_name);
    bool skip = false;
    if (excluded) {
      for (const String &ex : *excluded) {
        if (fileName.equals(ex) || ("/" + fileName).equals(ex)) {
          skip = true;
          break;
        }
      }
    }
    if (!skip)
      r->strOut[count++] = fileName;
  }
  closedir(dir);
  *r->countOut = count;
  r->ok = true;
}

static void dirEnumInner(Request *r) {
  snapshotFree();

  char full[PocketmageSD::kMaxPathLen];
  const char *path = resolve(r->path, full, sizeof(full));
  if (!path) {
    PM_LOGE(TAG, "dir enum: open failed: %s", r->path ? r->path : "(null)");
    *r->countOut = 0;
    r->ok = false;
    return;
  }

  DIR *dir = opendir(path);
  if (!dir) {
    PM_LOGE(TAG, "dir enum: open failed: %s", path);
    r->retVal = (size_t)-1;
    r->ok = false;
    return;
  }

  int cap = 16;
  s_snapshot = (SnapEntry *)malloc(cap * sizeof(SnapEntry));
  if (!s_snapshot) {
    closedir(dir);
    r->retVal = (size_t)-1;
    r->ok = false;
    return;
  }

  struct dirent *entry;
  while ((entry = readdir(dir)) != nullptr) {
    if (strcmp(entry->d_name, ".") == 0 || strcmp(entry->d_name, "..") == 0)
      continue;
    if (isMacMeta(entry->d_name))
      continue;
    if (s_snapshotCount == cap) {
      int next = cap * 2;
      SnapEntry *grown = (SnapEntry *)realloc(s_snapshot, next * sizeof(SnapEntry));
      if (!grown) {
        snapshotFree();
        closedir(dir);
        r->retVal = (size_t)-1;
        r->ok = false;
        return;
      }
      s_snapshot = grown;
      cap = next;
    }
    SnapEntry &e = s_snapshot[s_snapshotCount];
    strncpy(e.name, entry->d_name, kDirNameLen - 1);
    e.name[kDirNameLen - 1] = '\0';
    e.is_dir = (entry->d_type == DT_DIR);
    s_snapshotCount++;
  }
  closedir(dir);
  r->retVal = (size_t)s_snapshotCount;
  s_snapshotOwner = r->caller;
  r->ok = true;
}

static void dirListInner(Request *r) {
  auto *out = static_cast<std::vector<PocketmageSD::DirItem> *>(r->dirListOut);
  out->clear();

  char full[PocketmageSD::kMaxPathLen];
  const char *p = resolve(r->path, full, sizeof(full));
  if (!p) {
    PM_LOGE(TAG, "dir list: open failed: %s", r->path ? r->path : "(null)");
    r->ok = false;
    return;
  }

  DIR *dir = opendir(p);
  if (!dir) {
    PM_LOGE(TAG, "dir list: open failed: %s", full);
    r->ok = false;
    return;
  }

  struct dirent *entry;
  while ((entry = readdir(dir)) != nullptr) {
    if (strcmp(entry->d_name, ".") == 0 || strcmp(entry->d_name, "..") == 0)
      continue;
    if (isMacMeta(entry->d_name))
      continue;
    out->push_back({String(entry->d_name), entry->d_type == DT_DIR});
  }
  closedir(dir);
  r->ok = true;
}

static int mtarReadCb(mtar_t *tar, void *data, unsigned size) {
  FILE *f = (FILE *)tar->stream;
  return fread(data, 1, size, f) == size ? MTAR_ESUCCESS : MTAR_EREADFAIL;
}

static int mtarSeekCb(mtar_t *tar, unsigned pos) {
  FILE *f = (FILE *)tar->stream;
  return fseek(f, (long)pos, SEEK_SET) == 0 ? MTAR_ESUCCESS : MTAR_ESEEKFAIL;
}

static bool tarNameSafe(const char *name, char *out, size_t outSize) {
  if (!name || !name[0])
    return false;
  const char *p = name;
  while (*p == '/')
    p++;
  if (!*p)
    return false;
  if (strstr(p, "..") != nullptr)
    return false;
  snprintf(out, outSize, "%s", p);
  size_t n = strlen(out);
  while (n > 0 && out[n - 1] == '/')
    out[--n] = '\0';
  return n > 0;
}

static void ensureDirTree(const char *dir) {
  if (!dir)
    return;
  char prefix[PocketmageSD::kMaxPathLen];
  char resolved[PocketmageSD::kMaxPathLen];
  size_t n = strlen(dir);
  for (size_t i = 1; i < n; i++) {
    if (dir[i] == '/') {
      memcpy(prefix, dir, i);
      prefix[i] = '\0';
      const char *rp = resolve(prefix, resolved, sizeof(resolved));
      if (rp)
        mkdir(rp, 0777);
    }
  }
  const char *rp = resolve(dir, resolved, sizeof(resolved));
  if (rp)
    mkdir(rp, 0777);
}

static void mkdirParentsFor(const char *destDir, const char *name) {
  if (!destDir || !name)
    return;
  char path[PocketmageSD::kMaxPathLen];
  char resolved[PocketmageSD::kMaxPathLen];
  snprintf(path, sizeof(path), "%s/%s", destDir, name);
  size_t base = strlen(destDir) + 1;
  for (size_t i = base; i < strlen(path); i++) {
    if (path[i] == '/') {
      path[i] = '\0';
      const char *rp = resolve(path, resolved, sizeof(resolved));
      if (rp)
        mkdir(rp, 0777);
      path[i] = '/';
    }
  }
}

static void extractTarInner(Request *r) {
  char tarFull[PocketmageSD::kMaxPathLen];
  const char *tarPath = resolve(r->path, tarFull, sizeof(tarFull));
  if (!tarPath) {
    PM_LOGE(TAG, "tar not found: %s", r->path ? r->path : "(null)");
    r->ok = false;
    return;
  }
  const char *destDir = r->path2;

  struct stat st;
  if (stat(tarPath, &st) != 0) {
    PM_LOGE(TAG, "tar not found: %s", tarPath);
    r->ok = false;
    return;
  }
  const size_t tarSize = st.st_size;
  if (tarSize == 0) {
    PM_LOGE(TAG, "tar empty: %s", tarPath);
    r->ok = false;
    return;
  }

  FILE *tarFile = fopen(tarPath, "r");
  if (!tarFile) {
    PM_LOGE(TAG, "tar open failed: %s", tarPath);
    r->ok = false;
    return;
  }

  uint8_t *buf = (uint8_t *)malloc(kCopyChunk);
  if (!buf) {
    fclose(tarFile);
    r->ok = false;
    return;
  }

  mtar_t tar = {};
  tar.stream = tarFile;
  tar.read = mtarReadCb;
  tar.seek = mtarSeekCb;
  tar.close = nullptr;

  ensureDirTree(destDir);

  bool ok = true;
  mtar_header_t h;
  int err;
  while ((err = mtar_read_header(&tar, &h)) == MTAR_ESUCCESS) {
    char name[PocketmageSD::kMaxPathLen];
    if (!tarNameSafe(h.name, name, sizeof(name))) {
      PM_LOGE(TAG, "tar: unsafe entry name: %s", h.name);
      err = MTAR_EFAILURE;
      ok = false;
      break;
    }

    if (isMacMeta(name)) {
      PM_LOGI(TAG, "tar: skipping macOS metadata: %s", name);
    } else {
      char outPath[PocketmageSD::kMaxPathLen];
      snprintf(outPath, sizeof(outPath), "%s/%s", destDir, name);

      if (h.type == MTAR_TDIR) {
        ensureDirTree(outPath);
      } else if (h.type == MTAR_TREG) {
        mkdirParentsFor(destDir, name);
        char outFull[PocketmageSD::kMaxPathLen];
        const char *outResolved = resolve(outPath, outFull, sizeof(outFull));
        FILE *out = outResolved ? fopen(outResolved, "w") : nullptr;
        if (!out) {
          PM_LOGE(TAG, "tar: create failed: %s", outFull);
          ok = false;
          break;
        }
        size_t left = h.size;
        while (left > 0) {
          size_t want = left < kCopyChunk ? left : kCopyChunk;
          if (mtar_read_data(&tar, buf, want) != MTAR_ESUCCESS ||
              fwrite(buf, 1, want, out) != want) {
            ok = false;
            break;
          }
          left -= want;
        }
        fclose(out);
        if (!ok)
          break;
      }
    }

    unsigned headerPos = tar.last_header;
    unsigned dataLen = (h.size + 511u) & ~511u;
    if (mtar_seek(&tar, headerPos + 512u + dataLen) != MTAR_ESUCCESS) {
      ok = false;
      break;
    }

    if (r->progress)
      *r->progress = (int)((tar.pos * 100u) / tarSize);
  }

  if (err != MTAR_ENULLRECORD) {
    PM_LOGE(TAG, "tar: walk failed err=%d (%s)", err, mtar_strerror(err));
    ok = false;
  }

  if (r->progress)
    *r->progress = ok ? 100 : 0;
  free(buf);
  fclose(tarFile);
  r->ok = ok;
}

static void remount() {
  if (!directoryCloseAll())
    PM_LOGE(TAG, "directory close failed before remount");
  snapshotFree();
  // Close every open stream: a remount invalidates all existing FILE handles.
  for (int i = 0; i < kStreamSlots; i++) {
    if (s_streams[i] && !closeStream(i))
      PM_LOGE(TAG, "stream flush failed before remount: %d", i);
  }
  s_openStreams = 0;

  esp_vfs_fat_sdcard_unmount("/sdcard", getSdCard());
  bool ok = mountSdmmc();
  PM_LOGW(TAG, "remount %s", ok ? "ok" : "FAILED");
  s_needRemount = false;
}

static void exec(Request *r) {
  std::lock_guard<std::recursive_mutex> lock(s_executionMutex);
  // Reject foreign handles before recovery can invalidate any live resources.
  if (!canAccessResources(r)) {
    r->ok = false;
    r->retVal = 0;
    return;
  }
  if (r->op == Op::CloseCallerResources) {
    r->ok = closeOwnedResources(r->caller);
    return;
  }
  if (r->op == Op::BeginUsbAccess) {
    r->ok = unmountForUsb();
    return;
  }
  if (s_usbAccess) {
    r->ok = false;
    r->retVal = 0;
    return;
  }
  if (s_needRemount) {
    remount();
    if (!canAccessResources(r)) {
      r->ok = false;
      return;
    }
  }
  switch (r->op) {
  case Op::CloseCallerResources:
  case Op::BeginUsbAccess:
    // Handled before filesystem access and remount recovery above.
    break;
  case Op::Stat: {
    char full[PocketmageSD::kMaxPathLen];
    struct stat st;
    const char *p = resolve(r->path, full, sizeof(full));
    r->ok = p && stat(p, &st) == 0;
    r->retVal = r->ok ? (size_t)st.st_size : 0;
    if (r->isDirOut)
      *r->isDirOut = r->ok && S_ISDIR(st.st_mode);
    break;
  }

  case Op::Mkdir: {
    char full[PocketmageSD::kMaxPathLen];
    struct stat st;
    const char *p = resolve(r->path, full, sizeof(full));
    r->ok = p && ((mkdir(p, 0777) == 0) || (stat(p, &st) == 0));
    break;
  }

  case Op::Remove: {
    char full[PocketmageSD::kMaxPathLen];
    const char *p = resolve(r->path, full, sizeof(full));
    r->ok = p && remove(p) == 0;
    if (!r->ok)
      PM_LOGE(TAG, "Delete failed for %s", r->path);
    break;
  }

  case Op::RemoveTree: {
    char full[PocketmageSD::kMaxPathLen];
    const char *p = resolve(r->path, full, sizeof(full));
    r->ok = p && removeTreeInner(p);
    break;
  }

  case Op::Rename: {
    char full1[PocketmageSD::kMaxPathLen];
    char full2[PocketmageSD::kMaxPathLen];
    const char *p1 = resolve(r->path, full1, sizeof(full1));
    const char *p2 = resolve(r->path2, full2, sizeof(full2));
    r->ok = p1 && p2 && rename(p1, p2) == 0;
    if (!r->ok)
      PM_LOGE(TAG, "Rename failed: %s -> %s", r->path, r->path2);
    break;
  }

  case Op::CopyFile: {
    char full1[PocketmageSD::kMaxPathLen];
    char full2[PocketmageSD::kMaxPathLen];
    const char *p1 = resolve(r->path, full1, sizeof(full1));
    const char *p2 = resolve(r->path2, full2, sizeof(full2));
    r->ok = p1 && p2 && copyFileInner(p1, p2);
    break;
  }

  case Op::ListDir:
    listDirInner(r);
    break;

  case Op::ReadFileString: {
    char full[PocketmageSD::kMaxPathLen];
    const char *p = resolve(r->path, full, sizeof(full));
    if (!p) {
      PM_LOGE(TAG, "Failed to open file for reading: %s", r->path ? r->path : "(null)");
      r->strOut->clear();
      r->ok = false;
      break;
    }
    FILE *file = fopen(p, "r");
    if (!file) {
      PM_LOGE(TAG, "Failed to open file for reading: %s", p);
      r->strOut->clear();
      r->ok = false;
      break;
    }
    fseek(file, 0, SEEK_END);
    long fileSize = ftell(file);
    fseek(file, 0, SEEK_SET);
    if (fileSize <= 0) {
      fclose(file);
      r->strOut->clear();
      r->ok = true;
      break;
    }
    char *buf = (char *)malloc(fileSize + 1);
    if (buf) {
      size_t n = fread(buf, 1, fileSize, file);
      buf[n] = '\0';
      *r->strOut = String(buf);
      free(buf);
      r->ok = true;
    } else {
      r->strOut->clear();
      r->ok = false;
    }
    fclose(file);
    break;
  }

  case Op::WriteFile: {
    FILE *file = openResolved(r->path, "w");
    if (!file) {
      PM_LOGE(TAG, "Failed to open %s for writing", r->path);
      r->ok = false;
      break;
    }
    r->ok = fputs(r->text, file) >= 0;
    fclose(file);
    break;
  }

  case Op::AppendFile: {
    FILE *file = openResolved(r->path, "a");
    if (!file) {
      PM_LOGE(TAG, "Failed to open for appending: %s", r->path);
      r->ok = false;
      break;
    }
    r->ok = fputs(r->text, file) >= 0 && fputc('\n', file) != EOF;
    fclose(file);
    break;
  }

  case Op::ReadBinary: {
    char full[PocketmageSD::kMaxPathLen];
    const char *p = resolve(r->path, full, sizeof(full));
    FILE *f = p ? fopen(p, "r") : nullptr;
    if (!f) {
      if (errno == EIO)
        s_needRemount = true;
      PM_LOGE(TAG, "Failed to open file: %s", r->path);
      r->ok = false;
      break;
    }
    size_t n = streamFill(f, r->rbuf, r->len);
    fclose(f);
    r->retVal = n;
    r->ok = n == r->len;
    break;
  }

  case Op::WriteBinary:
  case Op::AppendBinary: {
    FILE *f = openResolved(r->path, r->op == Op::WriteBinary ? "w" : "a");
    if (!f) {
      PM_LOGE(TAG, "Failed to open for write: %s errno=%d", r->path, errno);
      r->ok = false;
      break;
    }
    const size_t n = r->len ? fwrite(r->wbuf, 1, r->len, f) : 0;
    const bool closed = fclose(f) == 0;
    r->retVal = n;
    r->ok = n == r->len && closed;
    break;
  }

  case Op::DirEnum:
    dirEnumInner(r);
    break;

  case Op::DirIteratorOpen:
    directoryOpen(r);
    break;
  case Op::DirIteratorRead:
    directoryRead(r);
    break;
  case Op::DirIteratorClose:
    r->ok = directoryClose(r->slot);
    break;

  case Op::DirEnumCount:
    r->retVal = static_cast<size_t>(s_snapshotCount);
    r->ok = true;
    break;

  case Op::DirEnumEntry:
    r->ok = copySnapshotEntry(r);
    break;

  case Op::DirList:
    dirListInner(r);
    break;

  case Op::DirEnumClose:
    snapshotFree();
    r->ok = true;
    break;

  case Op::ExtractTar:
    extractTarInner(r);
    break;

  case Op::StreamOpen: {
    int slot = -1;
    for (int i = 0; i < kStreamSlots; i++) {
      if (!s_streams[i]) {
        slot = i;
        break;
      }
    }
    if (slot < 0) {
      PM_LOGE(TAG, "stream: no free slot");
      r->retVal = (size_t)-1;
      r->ok = false;
      break;
    }
    char mode[2] = {(char)r->whence, '\0'};
    FILE *f = openResolved(r->path, mode);
    if (!f) {
      if (errno == EIO)
        s_needRemount = true;
      PM_LOGE(TAG, "stream: open failed: %s", r->path);
      r->retVal = (size_t)-1;
      r->ok = false;
      break;
    }
    s_streams[slot] = f;
    s_streamOwners[slot] = r->caller;
    s_openStreams++;
    r->retVal = (size_t)slot;
    r->ok = true;
    break;
  }

  case Op::StreamRead: {
    FILE *f = (r->slot >= 0 && r->slot < kStreamSlots) ? s_streams[r->slot] : nullptr;
    if (!f) {
      r->retVal = 0;
      r->ok = false;
      break;
    }
    r->retVal = streamFill(f, r->rbuf, r->len);
    r->ok = r->retVal > 0;
    break;
  }

  case Op::StreamWrite: {
    FILE *f = (r->slot >= 0 && r->slot < kStreamSlots) ? s_streams[r->slot] : nullptr;
    if (!f) {
      r->retVal = 0;
      r->ok = false;
      break;
    }
    r->retVal = fwrite(r->wbuf, 1, r->len, f);
    r->ok = r->retVal == r->len;
    break;
  }

  case Op::StreamSeek: {
    FILE *f = (r->slot >= 0 && r->slot < kStreamSlots) ? s_streams[r->slot] : nullptr;
    if (!f) {
      r->ok = false;
      break;
    }
    r->ok = fseek(f, r->off, r->whence) == 0;
    break;
  }

  case Op::StreamTell: {
    FILE *f = (r->slot >= 0 && r->slot < kStreamSlots) ? s_streams[r->slot] : nullptr;
    const long position = f ? ftell(f) : -1;
    r->retVal = position >= 0 ? static_cast<size_t>(position) : 0;
    r->ok = position >= 0;
    break;
  }

  case Op::StreamSize: {
    FILE *f = (r->slot >= 0 && r->slot < kStreamSlots) ? s_streams[r->slot] : nullptr;
    if (!f) {
      r->retVal = 0;
      r->ok = false;
      break;
    }
    long cur = ftell(f);
    if (cur < 0 || fseek(f, 0, SEEK_END) != 0) {
      r->retVal = 0;
      r->ok = false;
      break;
    }
    long size = ftell(f);
    const bool restored = fseek(f, cur, SEEK_SET) == 0;
    r->ok = size >= 0 && restored;
    r->retVal = r->ok ? static_cast<size_t>(size) : 0;
    break;
  }

  case Op::StreamClose: {
    r->ok = closeStream(r->slot);
    break;
  }
  }
}

} // namespace pm_sd_gateway
