#ifndef PM_LOG_API_H
#define PM_LOG_API_H

#ifdef __cplusplus
extern "C" {
#endif

/** Message severity, least to most detailed. Values match esp_log_level_t. */
enum pm_log_level {
  PM_LOG_LEVEL_NONE = 0,
  PM_LOG_LEVEL_ERROR = 1,
  PM_LOG_LEVEL_WARN = 2,
  PM_LOG_LEVEL_INFO = 3,
  PM_LOG_LEVEL_DEBUG = 4,
  PM_LOG_LEVEL_VERBOSE = 5,
};

/** Format and log one message under tag at level, PM_LOG_LEVEL_ERROR to PM_LOG_LEVEL_VERBOSE.
 * Safe from any task and from interrupts. An invalid level or null format is ignored, and
 * messages longer than 255 bytes are cut
 */
__attribute__((format(printf, 3, 4)))
void pm_log_write(int level, const char *tag, const char *format, ...);

#ifdef __cplusplus
}
#endif

// Compile-time cap, separate from CORE_DEBUG_LEVEL
#ifndef PM_LOG_MAX_LEVEL
#define PM_LOG_MAX_LEVEL PM_LOG_LEVEL_VERBOSE
#endif

#define PM_LOG_AT(level, tag, ...)                                                                 \
  do {                                                                                             \
    if ((level) <= PM_LOG_MAX_LEVEL)                                                               \
      pm_log_write((level), (tag), __VA_ARGS__);                                                   \
  } while (0)

#define PM_LOGE(tag, ...) PM_LOG_AT(PM_LOG_LEVEL_ERROR, tag, __VA_ARGS__)
#define PM_LOGW(tag, ...) PM_LOG_AT(PM_LOG_LEVEL_WARN, tag, __VA_ARGS__)
#define PM_LOGI(tag, ...) PM_LOG_AT(PM_LOG_LEVEL_INFO, tag, __VA_ARGS__)
#define PM_LOGD(tag, ...) PM_LOG_AT(PM_LOG_LEVEL_DEBUG, tag, __VA_ARGS__)
#define PM_LOGV(tag, ...) PM_LOG_AT(PM_LOG_LEVEL_VERBOSE, tag, __VA_ARGS__)

#endif
