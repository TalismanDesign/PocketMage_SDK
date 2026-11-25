#pragma once

#include <cstddef>
#include <cstdint>
#include <cstring>

namespace pm_audio {

inline const char *oggCodec(const uint8_t *data, size_t len) {
  if (!data || len < 27 || std::memcmp(data, "OggS", 4))
    return "not-ogg";
  const size_t headerLen = 27 + data[26];
  if (headerLen > len)
    return "truncated";
  static const uint8_t kVorbis[] = {0x01, 'v', 'o', 'r', 'b', 'i', 's'};
  static const uint8_t kFlac[] = {0x7F, 'F', 'L', 'A', 'C'};
  const size_t limit = len < 512 ? len : 512;
  for (size_t i = headerLen; i < limit; ++i) {
    if (limit - i >= 8 && !std::memcmp(data + i, "OpusHead", 8))
      return "opus";
    if (limit - i >= sizeof(kVorbis) && !std::memcmp(data + i, kVorbis, sizeof(kVorbis)))
      return "vorbis";
    if (limit - i >= sizeof(kFlac) && !std::memcmp(data + i, kFlac, sizeof(kFlac)))
      return "flac";
  }
  return "unknown";
}

} // namespace pm_audio
