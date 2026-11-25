#pragma once

#include <cstddef>
#include <cstdint>
#include <cstring>

namespace pm_audio {

inline uint16_t wavLe16(const uint8_t *p) {
  return static_cast<uint16_t>(p[0] | (uint16_t(p[1]) << 8));
}

inline uint32_t wavLe32(const uint8_t *p) {
  return uint32_t(p[0]) | (uint32_t(p[1]) << 8) | (uint32_t(p[2]) << 16) | (uint32_t(p[3]) << 24);
}

inline void wavPutLe16(uint8_t *p, uint16_t value) {
  p[0] = static_cast<uint8_t>(value);
  p[1] = static_cast<uint8_t>(value >> 8);
}

inline void wavPutLe32(uint8_t *p, uint32_t value) {
  for (unsigned i = 0; i < 4; ++i)
    p[i] = static_cast<uint8_t>(value >> (8 * i));
}

/** Rewrite a WAVE_FORMAT_EXTENSIBLE fmt chunk into the classic 16-byte form
 * See https://learn.microsoft.com/en-us/windows/win32/api/mmreg/ns-mmreg-waveformatextensible
 */
inline int normalizeWavHeader(uint8_t *data, size_t len) {
  static constexpr uint16_t kPcm = 1;
  static constexpr uint16_t kIeeeFloat = 3;
  static constexpr uint16_t kExtensible = 0xFFFE;
  static constexpr size_t kExtensibleFmtBytes = 40;
  static constexpr size_t kClassicFmtBytes = 16;
  static constexpr size_t kExtensionBytes = kExtensibleFmtBytes - kClassicFmtBytes;

  if (!data || len < 12 || std::memcmp(data, "RIFF", 4) || std::memcmp(data + 8, "WAVE", 4))
    return 0;

  size_t position = 12;
  while (len - position >= 8) {
    const uint32_t chunkLength = wavLe32(data + position + 4);
    const uint64_t body = uint64_t(position) + 8;
    if (body > len || chunkLength > len - body)
      return 0; // Header extends past the first window; leave it to the parser.
    if (!std::memcmp(data + position, "fmt ", 4)) {
      if (wavLe16(data + body) != kExtensible)
        return 0;
      if (chunkLength < kExtensibleFmtBytes)
        return -1;
      const uint16_t subformat = wavLe16(data + body + 24);
      if (subformat != kPcm && subformat != kIeeeFloat)
        return -1;
      wavPutLe16(data + body, subformat);
      wavPutLe32(data + position + 4, static_cast<uint32_t>(kClassicFmtBytes));
      const size_t removeAt = body + kClassicFmtBytes;
      const size_t trailing = len - (removeAt + kExtensionBytes);
      std::memmove(data + removeAt, data + removeAt + kExtensionBytes, trailing);
      const uint32_t riffLength = wavLe32(data + 4);
      if (riffLength >= kExtensionBytes)
        wavPutLe32(data + 4, riffLength - static_cast<uint32_t>(kExtensionBytes));
      return static_cast<int>(kExtensionBytes);
    }
    position = static_cast<size_t>(body + chunkLength + (chunkLength & 1));
    if (position > len)
      break;
  }
  return 0;
}

/** Convert little-endian IEEE-float samples to signed 16-bit PCM */
inline size_t wavFloatToPcm16(void *dest, const void *src, size_t sampleCount,
                              uint16_t bitsPerSample) {
  if (!dest || !src || (bitsPerSample != 32 && bitsPerSample != 64))
    return 0;
  const auto *input = static_cast<const uint8_t *>(src);
  auto *output = static_cast<uint8_t *>(dest);
  const size_t step = bitsPerSample / 8;
  for (size_t i = 0; i < sampleCount; ++i) {
    float value;
    if (bitsPerSample == 32) {
      uint32_t raw = 0;
      std::memcpy(&raw, input + i * step, sizeof(raw));
      std::memcpy(&value, &raw, sizeof(value));
    } else {
      uint64_t raw = 0;
      std::memcpy(&raw, input + i * step, sizeof(raw));
      double wide = 0;
      std::memcpy(&wide, &raw, sizeof(wide));
      value = static_cast<float>(wide);
    }
    int16_t pcm;
    if (!(value > -1.0f)) {
      pcm = value == value ? -32768 : 0;
    } else if (!(value < 1.0f)) {
      pcm = 32767;
    } else {
      const float scaled = value >= 0.0f ? value * 32767.0f : value * 32768.0f;
      pcm = static_cast<int16_t>(scaled >= 0.0f ? scaled + 0.5f : scaled - 0.5f);
    }
    std::memcpy(output + i * sizeof(pcm), &pcm, sizeof(pcm));
  }
  return sampleCount * sizeof(int16_t);
}

} // namespace pm_audio
