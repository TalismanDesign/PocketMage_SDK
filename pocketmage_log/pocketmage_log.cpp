//  dP                           //
//  88                           //
//  88        .d8888b. .d8888b.  //
//  88        88'  `88 88'  `88  //
//  88        88.  .88 88.  .88  //
//  88888888P `88888P' `8888P88  //
//                          .88  //
//                      d8888P   //

#include "pocketmage_log.h"

#include <pocketmage_sd/pocketmage_sd.h>
#include <pocketmage_sd/sd_gateway.h>

#include <esp_log.h>
#include <esp_rom_sys.h>
#include <esp_system.h>
#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>
#include <freertos/task.h>

#include <atomic>
#include <climits>
#include <cstdarg>
#include <cstdio>

namespace pm_log {
namespace {
constexpr const char *kTag = "LOG";
constexpr const char *kDirectory = "/logs";
constexpr const char *kUntaggedIdf = "IDF";
constexpr size_t kTagSlots = 24;
constexpr size_t kWriteChunk = 512;
constexpr size_t kUnknownSize = SIZE_MAX;
constexpr uint32_t kBatchMs = 1000;
constexpr uint32_t kFlushPollMs = 10;
constexpr uint32_t kShutdownFlushMs = 500;
constexpr uint32_t kWriterStack = 8192;
constexpr UBaseType_t kWriterPriority = 1;
constexpr BaseType_t kWriterCore = 1;
constexpr Level kDefaultLevel = Level::Info;

/** Boot lines are held until settings say whether file output is wanted. */
enum class FileState : uint8_t { Pending, On, Off };

portMUX_TYPE s_lock = portMUX_INITIALIZER_UNLOCKED;
TagLevels<kTagSlots> s_levels{kDefaultLevel};
LineQueue<kQueueBytes> s_queue;
TailBuffer<kTailBytes> s_tail;
FileState s_fileState = FileState::Pending;
TaskHandle_t s_writer = nullptr;
std::atomic<bool> s_writerInRequest{false};
std::atomic<bool> s_begun{false};

// Serializes level changes.
StaticSemaphore_t s_levelLockBuffer{};
SemaphoreHandle_t s_levelLock = xSemaphoreCreateMutexStatic(&s_levelLockBuffer);

// Writer-task state only.
char s_chunk[kWriteChunk];
size_t s_fileSize = kUnknownSize;
bool s_directoryReady = false;

uint32_t uptimeMs() {
  return esp_log_timestamp();
}

/** Lines raised by the writer's own card requests stay off the file, or a failing card
 * would feed the queue with reports about failing to drain it.
 */
bool fromWriterRequest() {
  const bool inRequest = s_writerInRequest.load(std::memory_order_acquire);
  const LogContext context = xPortInIsrContext() ? LogContext::Isr : LogContext::Task;
  const TaskHandle_t current = xTaskGetCurrentTaskHandle();
  return lineFromWriterRequest(inRequest, context, current == s_writer,
                               pm_sd_gateway::onGatewayTask());
}

/** Notify the writer from either a task or an interrupt. */
void wakeWriter(TaskHandle_t writer) {
  if (!xPortInIsrContext()) {
    xTaskNotifyGive(writer);
    return;
  }
  BaseType_t woken = pdFALSE;
  vTaskNotifyGiveFromISR(writer, &woken);
  if (woken)
    portYIELD_FROM_ISR();
}

void emit(Level level, const char *tag, char *message, size_t messageLength) {
  messageLength = cleanMessage(message, messageLength);
  char header[kHeaderCapacity];
  const size_t headerLength = formatHeader(header, uptimeMs(), level, tag);
  message[messageLength] = '\0';
  esp_rom_printf("%s%s\r\n", header, message);

  const bool toFile = !fromWriterRequest();
  bool wake = false;
  TaskHandle_t writer = nullptr;
  portENTER_CRITICAL_SAFE(&s_lock);
  s_tail.push(header, headerLength, message, messageLength);
  if (toFile && s_fileState != FileState::Off) {
    s_queue.push(header, headerLength, message, messageLength);
    wake = level <= Level::Warn || s_queue.used >= kQueueBytes / 2;
  }
  writer = s_fileState == FileState::On ? s_writer : nullptr;
  portEXIT_CRITICAL_SAFE(&s_lock);
  if (wake && writer)
    wakeWriter(writer);
}

bool allowed(Level level, const char *tag) {
  portENTER_CRITICAL_SAFE(&s_lock);
  const bool pass = s_levels.allows(level, tag);
  portEXIT_CRITICAL_SAFE(&s_lock);
  return pass;
}

/** ESP-IDF hands over its fully formatted line, so the level and tag are read back out of it. */
int idfHook(const char *format, va_list args) {
  char line[kMessageCapacity];
  const int count = std::vsnprintf(line, sizeof(line), format, args);
  const size_t length = storedLength(count, sizeof(line));
  if (!length)
    return count;
  Level level = Level::Info;
  const char *tag = kUntaggedIdf;
  const char *message = line;
  if (!parseIdfLine(line, &level, &tag, &message)) {
    if (allowed(level, tag))
      emit(level, tag, line, length);
    return count;
  }
  if (allowed(level, tag)) {
    // SAFETY: parseIdfLine returns a pointer inside line, so the offset is within length.
    const size_t offset = static_cast<size_t>(message - line);
    emit(level, tag, const_cast<char *>(message), length - offset);
  }
  return count;
}

bool cardAvailable() {
  return !PM_SD().getNoSD() && !pm_sd_gateway::usbAccessActive();
}

/** Run one card request with the writer's recursion guard raised. */
template <typename Request> bool cardRequest(Request request) {
  s_writerInRequest.store(true, std::memory_order_release);
  const bool ok = request();
  s_writerInRequest.store(false, std::memory_order_release);
  return ok;
}

bool prepareFile(size_t incoming) {
  if (!s_directoryReady) {
    s_directoryReady = cardRequest([] { return PM_SD().mkdirPath(kDirectory); });
    if (!s_directoryReady)
      return false;
  }
  if (s_fileSize == kUnknownSize) {
    size_t size = 0;
    bool isDir = false;
    const bool found = cardRequest([&] { return PM_SD().statPath(kFilePath, &size, &isDir); });
    if (found && isDir)
      return false;
    s_fileSize = found ? size : 0;
  }
  if (!needsRotation(s_fileSize, incoming, kFileLimitBytes))
    return true;
  const bool oldExists = cardRequest([] { return PM_SD().exists(kRotatedPath); });
  if (oldExists && !cardRequest([] { return PM_SD().removeFile(kRotatedPath); }))
    return false;
  if (!cardRequest([] { return PM_SD().renamePath(kFilePath, kRotatedPath); }))
    return false;
  s_fileSize = 0;
  return true;
}

/** Write queued lines until the queue is empty or the card refuses. Returns false on failure. */
bool drain() {
  for (;;) {
    portENTER_CRITICAL(&s_lock);
    const size_t length = s_queue.peek(s_chunk, sizeof(s_chunk));
    portEXIT_CRITICAL(&s_lock);
    if (!length)
      return true;
    const bool cardOk = cardAvailable();
    const bool prepOk = cardOk ? prepareFile(length) : false;
    if (!cardOk || !prepOk)
      return false;
    // SAFETY: s_chunk holds length bytes copied by peek.
    const auto *bytes = reinterpret_cast<const uint8_t *>(s_chunk);
    if (!cardRequest([&] { return PM_SD().appendBinaryFile(kFilePath, bytes, length); })) {
      // The file or directory may have been removed under us; look again on the next pass.
      s_fileSize = kUnknownSize;
      s_directoryReady = false;
      return false;
    }
    s_fileSize += length;
    portENTER_CRITICAL(&s_lock);
    s_queue.consume(length);
    portEXIT_CRITICAL(&s_lock);
  }
}

void writerTask(void *) {
  uint32_t waitMs = kBatchMs;
  for (;;) {
    ulTaskNotifyTake(pdTRUE, pdMS_TO_TICKS(waitMs));
    if (!fileEnabled()) {
      waitMs = kBatchMs;
      continue;
    }
    if (drain()) {
      waitMs = kBatchMs;
      continue;
    }
    waitMs = kBatchMs;
  }
}

void shutdownFlush() {
  flush(kShutdownFlushMs);
}

bool startWriter() {
  if (s_writer)
    return true;
  TaskHandle_t handle = nullptr;
  if (xTaskCreatePinnedToCore(writerTask, "pm_log", kWriterStack, nullptr, kWriterPriority, &handle,
                              kWriterCore) != pdPASS) {
    PM_LOGE(kTag, "Log writer task could not start; file logging stays off");
    return false;
  }
  portENTER_CRITICAL(&s_lock);
  s_writer = handle;
  portEXIT_CRITICAL(&s_lock);
  return true;
}
} // namespace

void begin() {
  if (s_begun.exchange(true))
    return;
  esp_log_set_vprintf(idfHook);
  setLevel("*", PM_LOG_LEVEL_INFO);
  if (esp_register_shutdown_handler(shutdownFlush) != ESP_OK)
    PM_LOGW(kTag, "No shutdown slot left; logs queued at restart may be lost");
}

void setFileEnabled(bool enabled) {
  if (enabled && !startWriter())
    enabled = false;
  portENTER_CRITICAL(&s_lock);
  s_fileState = enabled ? FileState::On : FileState::Off;
  if (!enabled)
    s_queue.clear();
  const TaskHandle_t writer = s_writer;
  portEXIT_CRITICAL(&s_lock);
  if (enabled && writer)
    xTaskNotifyGive(writer);
}

bool fileEnabled() {
  portENTER_CRITICAL(&s_lock);
  const bool enabled = s_fileState == FileState::On;
  portEXIT_CRITICAL(&s_lock);
  return enabled;
}

bool setLevel(const char *tag, pm_log_level level) {
  if (level < PM_LOG_LEVEL_NONE || level > PM_LOG_LEVEL_VERBOSE)
    return false;
  xSemaphoreTake(s_levelLock, portMAX_DELAY);
  portENTER_CRITICAL(&s_lock);
  const bool stored = s_levels.set(tag, static_cast<Level>(level));
  portEXIT_CRITICAL(&s_lock);
  if (stored) {
    esp_log_level_set(tag ? tag : "*", static_cast<esp_log_level_t>(level));
    if (!tag || std::strcmp(tag, "*") == 0)
      replayLevels(s_levels, [](const char *name, pm_log_level entryLevel) {
        esp_log_level_set(name, static_cast<esp_log_level_t>(entryLevel));
      });
  }
  xSemaphoreGive(s_levelLock);
  return stored;
}

pm_log_level level(const char *tag) {
  portENTER_CRITICAL(&s_lock);
  const Level current = s_levels.levelFor(tag);
  portEXIT_CRITICAL(&s_lock);
  return static_cast<pm_log_level>(current);
}

bool flush(uint32_t timeoutMs) {
  portENTER_CRITICAL(&s_lock);
  const TaskHandle_t writer = s_fileState == FileState::On ? s_writer : nullptr;
  portEXIT_CRITICAL(&s_lock);
  if (!writer || writer == xTaskGetCurrentTaskHandle() || !cardAvailable())
    return false;
  xTaskNotifyGive(writer);
  const TickType_t start = xTaskGetTickCount();
  for (;;) {
    portENTER_CRITICAL(&s_lock);
    const bool empty = s_queue.used == 0;
    portEXIT_CRITICAL(&s_lock);
    if (empty)
      return true;
    if (xTaskGetTickCount() - start >= pdMS_TO_TICKS(timeoutMs))
      return false;
    vTaskDelay(pdMS_TO_TICKS(kFlushPollMs));
  }
}

size_t copyTail(char *out, size_t capacity) {
  return s_tail.copy(out, capacity);
}

} // namespace pm_log

extern "C" void pm_log_write(int level, const char *tag, const char *format, ...) {
  using namespace pm_log;
  if (!messageLevel(level) || !format)
    return;
  const Level typed = static_cast<Level>(level);
  if (!allowed(typed, tag))
    return;
  char message[kMessageCapacity];
  va_list args;
  va_start(args, format);
  const int count = std::vsnprintf(message, sizeof(message), format, args);
  va_end(args);
  emit(typed, tag, message, storedLength(count, sizeof(message)));
}
