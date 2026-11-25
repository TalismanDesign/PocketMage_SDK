#pragma once

#include <cctype>
#include <cstdint>
#include <cstring>

namespace pm_audio {

enum class AudioFormat {
  None,
  Mp3,
  Wav,
  Ogg,
};

/** Map a file path to a supported format; unsupported extensions return None. */
inline AudioFormat formatFromPath(const char *path) {
  if (!path)
    return AudioFormat::None;
  const char *dot = std::strrchr(path, '.');
  if (!dot)
    return AudioFormat::None;
  const size_t length = std::strlen(dot + 1);
  if (length != 3 && length != 4)
    return AudioFormat::None;
  char ext[5] = {};
  for (size_t i = 0; i < length; ++i)
    ext[i] = static_cast<char>(std::tolower(static_cast<unsigned char>(dot[1 + i])));
  if (!std::strcmp(ext, "mp3"))
    return AudioFormat::Mp3;
  if (!std::strcmp(ext, "wav"))
    return AudioFormat::Wav;
  if (!std::strcmp(ext, "ogg") || !std::strcmp(ext, "opus"))
    return AudioFormat::Ogg;
  return AudioFormat::None;
}

inline uint32_t decoderStackBytes(AudioFormat format) {
  switch (format) {
  case AudioFormat::Mp3:
    return 8192;
  case AudioFormat::Wav:
    return 6144;
  case AudioFormat::Ogg:
    return 20 * 1024;
  case AudioFormat::None:
    break;
  }
  return 0;
}

/** Decoder feed size in bytes */
inline uint32_t readChunkBytes(AudioFormat format) {
  switch (format) {
  case AudioFormat::Mp3:
    return 4096;
  case AudioFormat::Wav:
    return 4096;
  case AudioFormat::Ogg:
    return 512;
  case AudioFormat::None:
    break;
  }
  return 0;
}

/** Short lowercase name used in decoder logs. */
inline const char *formatName(AudioFormat format) {
  switch (format) {
  case AudioFormat::Mp3:
    return "mp3";
  case AudioFormat::Wav:
    return "wav";
  case AudioFormat::Ogg:
    return "ogg";
  case AudioFormat::None:
    break;
  }
  return "none";
}
} // namespace pm_audio
