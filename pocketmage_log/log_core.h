#pragma once

#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstring>

#include "log_api.h"

namespace pm_log {

enum class Level : uint8_t {
  None = PM_LOG_LEVEL_NONE,
  Error = PM_LOG_LEVEL_ERROR,
  Warn = PM_LOG_LEVEL_WARN,
  Info = PM_LOG_LEVEL_INFO,
  Debug = PM_LOG_LEVEL_DEBUG,
  Verbose = PM_LOG_LEVEL_VERBOSE,
};

/** Longest tag kept, including NUL. */
constexpr size_t kTagCapacity = 16;
/** Header buffer size */
constexpr size_t kHeaderCapacity = 32;
/** Message buffer size, including NUL. Longer messages are cut. */
constexpr size_t kMessageCapacity = 256;

/** Return the one-letter name used in log lines, or '?' for a value that is not a level. */
inline char levelLetter(Level level) {
  switch (level) {
  case Level::Error:
    return 'E';
  case Level::Warn:
    return 'W';
  case Level::Info:
    return 'I';
  case Level::Debug:
    return 'D';
  case Level::Verbose:
    return 'V';
  case Level::None:
    break;
  }
  return '?';
}

/** Map a level letter back to a Level. Returns false and leaves level untouched otherwise. */
inline bool levelFromLetter(char letter, Level *level) {
  switch (letter) {
  case 'E':
    *level = Level::Error;
    return true;
  case 'W':
    *level = Level::Warn;
    return true;
  case 'I':
    *level = Level::Info;
    return true;
  case 'D':
    *level = Level::Debug;
    return true;
  case 'V':
    *level = Level::Verbose;
    return true;
  default:
    return false;
  }
}

inline bool messageLevel(int value) {
  return value >= static_cast<int>(Level::Error) && value <= static_cast<int>(Level::Verbose);
}

inline char printable(char c) {
  const unsigned char byte = static_cast<unsigned char>(c);
  return byte < 0x20 || byte == 0x7f ? ' ' : c;
}

inline size_t formatHeader(char (&out)[kHeaderCapacity], uint32_t uptimeMs, Level level,
                           const char *tag) {
  char cleanTag[kTagCapacity];
  size_t tagLength = 0;
  for (; tag && tag[tagLength] && tagLength + 1 < sizeof(cleanTag); ++tagLength)
    cleanTag[tagLength] = printable(tag[tagLength]);
  cleanTag[tagLength] = '\0';
  const int count =
      std::snprintf(out, sizeof(out), "%lu %c [%s] ", static_cast<unsigned long>(uptimeMs),
                    levelLetter(level), cleanTag);
  return count < 0 ? 0 : static_cast<size_t>(count);
}

/** Clean length bytes of message in place */
inline size_t cleanMessage(char *message, size_t length) {
  if (!message)
    return 0;
  while (length && (message[length - 1] == '\n' || message[length - 1] == '\r'))
    --length;
  for (size_t i = 0; i < length; ++i)
    message[i] = printable(message[i]);
  return length;
}

inline size_t storedLength(int count, size_t capacity) {
  if (count < 0 || !capacity)
    return 0;
  return static_cast<size_t>(count) < capacity ? static_cast<size_t>(count) : capacity - 1;
}

inline bool parseIdfLine(char *line, Level *level, const char **tag, const char **message) {
  Level parsed = Level::None;
  if (!line || !levelFromLetter(line[0], &parsed) || line[1] != ' ' || line[2] != '(')
    return false;
  char *cursor = line + 3;
  if (*cursor < '0' || *cursor > '9')
    return false;
  while (*cursor >= '0' && *cursor <= '9')
    ++cursor;
  if (cursor[0] != ')' || cursor[1] != ' ')
    return false;
  char *tagStart = cursor + 2;
  char *separator = std::strstr(tagStart, ": ");
  if (!separator || separator == tagStart)
    return false;
  *separator = '\0';
  *level = parsed;
  *tag = tagStart;
  *message = separator + 2;
  return true;
}

inline bool needsRotation(size_t fileSize, size_t incoming, size_t limit) {
  return fileSize > 0 && fileSize + incoming > limit;
}

enum class LogContext : uint8_t { Task, Isr };

inline bool lineFromWriterRequest(bool inRequest, LogContext context, bool currentIsWriter,
                                  bool currentIsGateway) {
  return inRequest && context == LogContext::Task && (currentIsWriter || currentIsGateway);
}

/** Per-tag maximum levels, with a fallback for every tag not in the table */
template <size_t N> struct TagLevels {
  struct Entry {
    char tag[kTagCapacity];
    Level level;
  };

  explicit constexpr TagLevels(Level fallbackLevel) : fallback(fallbackLevel) {
  }

  /** Return the level set for tag, or the fallback. */
  Level levelFor(const char *tag) const {
    for (size_t i = 0; tag && i < count; ++i) {
      if (std::strcmp(entries[i].tag, tag) == 0)
        return entries[i].level;
    }
    return fallback;
  }

  /** Return whether a message at level under tag passes the filter. */
  bool allows(Level level, const char *tag) const {
    return level != Level::None && level <= levelFor(tag);
  }

  /** Set the level for tag, or the fallback when tag is null or "*".
   * @returns false when the tag is empty, too long, or the table is full.
   */
  bool set(const char *tag, Level level) {
    if (!tag || std::strcmp(tag, "*") == 0) {
      fallback = level;
      return true;
    }
    const size_t length = std::strlen(tag);
    if (!length || length >= kTagCapacity)
      return false;
    for (size_t i = 0; i < count; ++i) {
      if (std::strcmp(entries[i].tag, tag) == 0) {
        entries[i].level = level;
        return true;
      }
    }
    if (count == N)
      return false;
    std::memcpy(entries[count].tag, tag, length + 1);
    entries[count].level = level;
    ++count;
    return true;
  }

  size_t copyEntries(Entry *out, size_t capacity) const {
    const size_t copied = count < capacity ? count : capacity;
    if (copied && out)
      std::memcpy(out, entries, copied * sizeof(Entry));
    return copied;
  }

  Entry entries[N]{};
  size_t count = 0;
  Level fallback;
};

/** Replay stored tag levels through apply, oldest entry first */
template <size_t N, typename Apply> size_t replayLevels(const TagLevels<N> &levels, Apply apply) {
  typename TagLevels<N>::Entry entries[N];
  const size_t count = levels.copyEntries(entries, N);
  for (size_t i = 0; i < count; ++i)
    apply(entries[i].tag, static_cast<pm_log_level>(entries[i].level));
  return count;
}

/** Byte queue of whole lines waiting for the file writer. A line that does not fit is dropped
 * and counted, and the next line that fits is preceded by a note giving the count */
template <size_t N> struct LineQueue {
  bool push(const char *header, size_t headerLength, const char *message, size_t messageLength) {
    char note[48];
    size_t noteLength = 0;
    if (dropped)
      noteLength = storedLength(std::snprintf(note, sizeof(note), "-- %lu log lines dropped --\n",
                                              static_cast<unsigned long>(dropped)),
                                sizeof(note));
    const size_t needed = noteLength + headerLength + messageLength + 1;
    if (needed > N - used) {
      ++dropped;
      return false;
    }
    append(note, noteLength);
    append(header, headerLength);
    append(message, messageLength);
    append("\n", 1);
    dropped = 0;
    return true;
  }

  /** Copy up to capacity queued bytes into out, oldest first, without removing them.
   * @returns the bytes copied
   */
  size_t peek(char *out, size_t capacity) const {
    size_t copied = 0;
    for (; copied < capacity && copied < used; ++copied)
      out[copied] = data[(head + copied) % N];
    return copied;
  }

  /** Remove up to count bytes from the front. */
  void consume(size_t count) {
    if (count > used)
      count = used;
    head = (head + count) % N;
    used -= count;
  }

  void clear() {
    head = 0;
    used = 0;
    dropped = 0;
  }

  void append(const char *text, size_t length) {
    for (size_t i = 0; i < length; ++i)
      data[(head + used + i) % N] = text[i];
    used += length;
  }

  char data[N]{};
  size_t head = 0;
  size_t used = 0;
  uint32_t dropped = 0;
};

template <size_t N> struct TailBuffer {
  void push(const char *header, size_t headerLength, const char *message, size_t messageLength) {
    append(header, headerLength);
    append(message, messageLength);
    append("\n", 1);
  }

  /** Copy the retained text into out, oldest first, and NUL-terminate it */
  size_t copy(char *out, size_t capacity) const {
    if (!out || !capacity)
      return 0;
    const size_t end = next % N;
    const bool full = wrapped;
    const size_t start = full ? end : 0;
    const size_t total = full ? N : end;
    size_t skip = total > capacity - 1 ? total - (capacity - 1) : 0;
    if (full || skip) {
      while (skip < total && data[(start + skip) % N] != '\n')
        ++skip;
      if (skip < total)
        ++skip;
    }
    size_t length = 0;
    for (size_t i = skip; i < total && length + 1 < capacity; ++i)
      out[length++] = data[(start + i) % N];
    out[length] = '\0';
    return length;
  }

  void append(const char *text, size_t length) {
    for (size_t i = 0; i < length; ++i) {
      data[next] = text[i];
      next = (next + 1) % N;
      if (next == 0)
        wrapped = true;
    }
  }

  char data[N]{};
  size_t next = 0;
  bool wrapped = false;
};

} // namespace pm_log
