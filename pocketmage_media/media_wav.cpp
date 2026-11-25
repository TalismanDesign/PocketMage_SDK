#include "media_endian.h"
#include "media_reader.h"

#include <cstring>

namespace {
class WindowReader : public pm_media::Reader {
public:
  WindowReader(pm_media::Reader &base, uint32_t start, uint32_t length)
      : base_(base), start_(start), length_(length) {
  }
  uint32_t size() const override {
    return length_;
  }
  bool readAt(uint32_t offset, void *out, size_t count) override {
    if (offset > length_ || count > length_ - offset)
      return false;
    return base_.readAt(start_ + offset, out, count);
  }

private:
  pm_media::Reader &base_;
  uint32_t start_, length_;
};

void copyText(const uint8_t *bytes, uint32_t length, char *out, size_t capacity) {
  const size_t limit = length < capacity - 1 ? length : capacity - 1;
  size_t used = 0;
  while (used < limit && bytes[used]) {
    out[used] = static_cast<char>(bytes[used]);
    ++used;
  }
  out[used] = 0;
}

constexpr uint16_t kWavPcm = 1;
constexpr uint16_t kWavIeeeFloat = 3;
constexpr uint16_t kWavExtensible = 0xFFFE;
constexpr uint32_t kExtensibleFmtBytes = 40;

void infoList(pm_media::Reader &reader, uint32_t start, uint32_t end, MediaInfo &out) {
  if (end - start < 4)
    return;
  uint8_t magic[4];
  if (!reader.readAt(start, magic, sizeof(magic)) || std::memcmp(magic, "INFO", 4))
    return;
  uint32_t position = start + 4;
  while (end - position >= 8) {
    uint8_t header[8];
    if (!reader.readAt(position, header, sizeof(header)))
      return;
    const uint32_t length = pm_media::little32(header + 4);
    const uint64_t body = uint64_t(position) + 8;
    if (body > end || length > end - body)
      return;
    const uint32_t next = static_cast<uint32_t>(body + length + (length & 1));
    if (next > end)
      return;
    char *target = nullptr;
    if (!std::memcmp(header, "INAM", 4))
      target = out.title;
    if (!std::memcmp(header, "IART", 4))
      target = out.artist;
    if (!std::memcmp(header, "IPRD", 4))
      target = out.album;
    if (target && length) {
      uint8_t bytes[128];
      const size_t count = length < sizeof(bytes) ? length : sizeof(bytes);
      if (!reader.readAt(static_cast<uint32_t>(body), bytes, count))
        return;
      copyText(bytes, static_cast<uint32_t>(count), target, sizeof(out.title));
    }
    position = next;
  }
}

bool parseWav(pm_media::Reader &reader, pm_media::WavFormat *format, MediaInfo *info) {
  using pm_media::little16;
  using pm_media::little32;
  if (reader.size() < 12)
    return false;
  uint8_t header[12];
  if (!reader.readAt(0, header, sizeof(header)) || std::memcmp(header, "RIFF", 4) ||
      std::memcmp(header + 8, "WAVE", 4))
    return false;

  bool haveFormat = false;
  uint32_t audioOffset = 0;
  uint64_t dataBytes = 0, id3Duration = 0;
  uint32_t position = 12;
  while (reader.size() - position >= 8) {
    uint8_t chunk[8];
    if (!reader.readAt(position, chunk, sizeof(chunk)))
      return false;
    const uint32_t length = little32(chunk + 4);
    const uint64_t body = uint64_t(position) + 8;
    if (body > reader.size() || length > reader.size() - body)
      return false;
    uint64_t next = body + length + (length & 1);
    if (next > reader.size())
      next = body + length;

    if (!std::memcmp(chunk, "fmt ", 4) && length >= 16 && format) {
      uint8_t bytes[kExtensibleFmtBytes] = {};
      const uint32_t count = length < sizeof(bytes) ? length : sizeof(bytes);
      if (!reader.readAt(static_cast<uint32_t>(body), bytes, count))
        return false;
      uint16_t codec = little16(bytes);
      if (codec == kWavExtensible) {
        if (length < kExtensibleFmtBytes)
          return false;
        codec = little16(bytes + 24); // SubFormat GUID Data1 holds the real codec.
      }
      if (codec != kWavPcm && codec != kWavIeeeFloat)
        return false;
      const uint32_t rate = little32(bytes + 4);
      const uint16_t blockAlign = little16(bytes + 12);
      if (!rate || !blockAlign)
        return false;
      format->sampleRate = rate;
      format->channels = little16(bytes + 2);
      format->bitsPerSample = little16(bytes + 14);
      format->blockAlign = blockAlign;
      format->codec = codec;
      haveFormat = true;
    } else if (!std::memcmp(chunk, "data", 4)) {
      dataBytes = length;
      audioOffset = static_cast<uint32_t>(body);
      if (format) {
        format->dataOffset = audioOffset;
        format->dataBytes = static_cast<uint32_t>(length);
      }
    } else if (info && !std::memcmp(chunk, "LIST", 4)) {
      infoList(reader, static_cast<uint32_t>(body), static_cast<uint32_t>(body + length), *info);
    } else if (info && !std::memcmp(chunk, "id3 ", 4) && length >= 10) {
      WindowReader window(reader, static_cast<uint32_t>(body), length);
      MediaInfo tags{};
      if (readId3(window, tags)) {
        if (!info->title[0])
          std::strncpy(info->title, tags.title, sizeof(info->title));
        if (!info->artist[0])
          std::strncpy(info->artist, tags.artist, sizeof(info->artist));
        if (!info->album[0])
          std::strncpy(info->album, tags.album, sizeof(info->album));
        if (tags.coverLength) {
          info->coverOffset = static_cast<uint32_t>(body) + tags.coverOffset;
          info->coverLength = tags.coverLength;
          info->coverFormat = tags.coverFormat;
        }
        id3Duration = tags.durationMs;
      }
    }
    position = static_cast<uint32_t>(next);
    if (next > reader.size())
      break;
  }
  if (!haveFormat)
    return false;
  if (info) {
    info->audioOffset = audioOffset;
    info->durationMs =
        dataBytes ? dataBytes / format->blockAlign * 1000 / format->sampleRate : id3Duration;
  }
  return true;
}
} // namespace

bool pm_media::wavFormat(Reader &reader, WavFormat &out) {
  WavFormat format{};
  if (!parseWav(reader, &format, nullptr))
    return false;
  out = format;
  return true;
}

bool pm_media::wavInfo(Reader &reader, MediaInfo &out) {
  MediaInfo info{};
  WavFormat format{};
  if (!parseWav(reader, &format, &info))
    return false;
  out = info;
  return true;
}
