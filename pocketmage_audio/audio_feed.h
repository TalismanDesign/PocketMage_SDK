#pragma once

#include <cstdint>

namespace pm_audio {

// Bytes to request from storage while keeping unconsumed parser bytes as is
inline uint32_t feedRequest(uint32_t capacity, uint32_t leftover) {
  return leftover < capacity ? capacity - leftover : 0;
}

struct FeedState {
  uint32_t avail;  // Bytes to hand to the simple decoder, including leftover
  bool eos;        // Decoder must flush cached data after this call
  bool storageEof; // Storage has already reported end of file
};

inline FeedState updateFeed(uint32_t capacity, uint32_t leftover, bool storageEof, int readLen) {
  const uint32_t requested = feedRequest(capacity, leftover);
  if (!requested)
    return {capacity, storageEof, storageEof};
  const bool eof = readLen < 0 || static_cast<uint32_t>(readLen) < requested;
  const uint32_t bytes = readLen > 0 ? static_cast<uint32_t>(readLen) : 0;
  return {leftover + bytes, eof, storageEof || eof};
}

inline bool parserStalled(uint32_t capacity, uint32_t leftover, bool attempted, uint32_t consumed,
                          uint32_t decoded) {
  return attempted && consumed == 0 && decoded == 0 && leftover >= capacity;
}

struct ParserRetryState {
  uint32_t idleCalls = 0;
  uint32_t drainCalls = 0;

  bool shouldRetry(uint32_t consumed, uint32_t decoded, uint32_t maxIdleCalls,
                   uint32_t maxDrainCalls) {
    if (consumed != 0) {
      idleCalls = 0;
      drainCalls = 0;
      return true;
    }
    if (decoded != 0) {
      idleCalls = 0;
      return ++drainCalls < maxDrainCalls;
    }
    return ++idleCalls < maxIdleCalls;
  }
};

} // namespace pm_audio
