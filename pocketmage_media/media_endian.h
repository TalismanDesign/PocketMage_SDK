#pragma once

#include <cstdint>

namespace pm_media {
inline uint16_t little16(const uint8_t *p) {
  return uint16_t(p[0]) | (uint16_t(p[1]) << 8);
}

inline uint32_t little32(const uint8_t *p) {
  return uint32_t(p[0]) | (uint32_t(p[1]) << 8) | (uint32_t(p[2]) << 16) | (uint32_t(p[3]) << 24);
}

inline uint64_t little64(const uint8_t *p) {
  return uint64_t(little32(p)) | (uint64_t(little32(p + 4)) << 32);
}

inline uint32_t big32(const uint8_t *p) {
  return (uint32_t(p[0]) << 24) | (uint32_t(p[1]) << 16) | (uint32_t(p[2]) << 8) | uint32_t(p[3]);
}
} // namespace pm_media
