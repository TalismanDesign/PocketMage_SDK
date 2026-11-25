#include "media_endian.h"
#include "media_reader.h"

#include <algorithm>
#include <cstring>

namespace {
constexpr uint32_t kCommentFields = 1024;
constexpr uint32_t kPicturePrefixBytes = 512;
constexpr uint32_t kGranuleSearchBytes = 64 * 1024;
constexpr uint32_t kGranuleWindowBytes = 1024;

class PacketCursor {
public:
  PacketCursor(pm_media::Reader &reader, uint32_t size) : reader_(reader), size_(size) {
  }

  bool nextPacket() {
    packetDone_ = false;
    return true;
  }

  bool byte(uint8_t &out) {
    if (packetDone_ || !segment())
      return false;
    if (!reader_.readAt(position_, &out, 1))
      return false;
    ++position_;
    if (!--segmentLeft_ && segments_[segmentIndex_ - 1] < 255)
      packetDone_ = true;
    return true;
  }

  bool read(void *out, size_t count) {
    auto *bytes = static_cast<uint8_t *>(out);
    for (size_t i = 0; i < count; ++i)
      if (!byte(bytes[i]))
        return false;
    return true;
  }

  bool skip(uint32_t count) {
    uint8_t ignored;
    for (uint32_t i = 0; i < count; ++i)
      if (!byte(ignored))
        return false;
    return true;
  }

  uint32_t position() const {
    return position_;
  }

private:
  bool segment() {
    while (!segmentLeft_) {
      if (segmentIndex_ >= segmentCount_ && !loadPage())
        return false;
      segmentLeft_ = segments_[segmentIndex_++];
      if (!segmentLeft_) {
        packetDone_ = true;
        return false;
      }
    }
    return true;
  }

  bool loadPage() {
    if (size_ - nextPage_ < 27)
      return false;
    uint8_t header[27];
    if (!reader_.readAt(nextPage_, header, sizeof(header)) || std::memcmp(header, "OggS", 4) ||
        header[4])
      return false;
    const uint32_t sequence = pm_media::little32(header + 18);
    if (haveSequence_ && sequence != sequence_ + 1)
      return false;
    sequence_ = sequence;
    haveSequence_ = true;
    const uint32_t count = header[26];
    if (count && size_ - nextPage_ - 27 < count)
      return false;
    if (count && !reader_.readAt(nextPage_ + 27, segments_, count))
      return false;
    segmentCount_ = count;
    segmentIndex_ = 0;
    position_ = nextPage_ + 27 + count;
    uint32_t payload = 0;
    for (uint32_t i = 0; i < count; ++i)
      payload += segments_[i];
    if (payload > size_ - position_)
      return false;
    nextPage_ = position_ + payload;
    return true;
  }

  pm_media::Reader &reader_;
  uint32_t size_, nextPage_ = 0, position_ = 0;
  uint32_t sequence_ = 0;
  bool haveSequence_ = false;
  uint8_t segments_[255] = {};
  uint32_t segmentCount_ = 0, segmentIndex_ = 0, segmentLeft_ = 0;
  bool packetDone_ = true;
};

uint32_t cursor32(PacketCursor &cursor) {
  uint8_t bytes[4];
  return cursor.read(bytes, sizeof(bytes)) ? pm_media::little32(bytes) : 0;
}

uint32_t cursor32WithFirst(PacketCursor &cursor, uint8_t first) {
  uint8_t bytes[4] = {first, 0, 0, 0};
  return cursor.read(bytes + 1, 3) ? pm_media::little32(bytes) : 0;
}

int base64Value(uint8_t c) {
  if (c >= 'A' && c <= 'Z')
    return c - 'A';
  if (c >= 'a' && c <= 'z')
    return c - 'a' + 26;
  if (c >= '0' && c <= '9')
    return c - '0' + 52;
  if (c == '+')
    return 62;
  if (c == '/')
    return 63;
  return -1;
}

bool decodeBase64(const uint8_t *chars, uint32_t count, uint8_t *out, uint32_t &decoded) {
  decoded = 0;
  if (count % 4)
    return false;
  for (uint32_t i = 0; i < count; i += 4) {
    const int a = base64Value(chars[i]), b = base64Value(chars[i + 1]);
    const int c = chars[i + 2] == '=' ? 0 : base64Value(chars[i + 2]);
    const int d = chars[i + 3] == '=' ? 0 : base64Value(chars[i + 3]);
    if (a < 0 || b < 0 || c < 0 || d < 0)
      return false;
    if (chars[i + 2] == '=' && chars[i + 3] != '=')
      return false;
    out[decoded++] = static_cast<uint8_t>((a << 2) | (b >> 4));
    if (chars[i + 2] != '=')
      out[decoded++] = static_cast<uint8_t>((b << 4) | (c >> 2));
    if (chars[i + 3] != '=')
      out[decoded++] = static_cast<uint8_t>((c << 6) | d);
  }
  return true;
}

bool keyEquals(const char *key, const char *name) {
  for (; *key && *name; ++key, ++name) {
    char left = *key, right = *name;
    if (left >= 'A' && left <= 'Z')
      left = static_cast<char>(left - 'A' + 'a');
    if (right >= 'A' && right <= 'Z')
      right = static_cast<char>(right - 'A' + 'a');
    if (left != right)
      return false;
  }
  return *key == 0 && *name == 0;
}

bool flacPicture(pm_media::Reader &reader, uint32_t offset, uint32_t length, MediaInfo &out) {
  if (length % 4 || length < 8)
    return false;
  uint8_t chars[kPicturePrefixBytes];
  uint8_t decoded[kPicturePrefixBytes / 4 * 3];
  const uint32_t read = std::min<uint32_t>(length, sizeof(chars));
  if (!reader.readAt(offset, chars, read))
    return false;
  uint32_t decodedBytes = 0;
  if (!decodeBase64(chars, read, decoded, decodedBytes))
    return false;
  if (decodedBytes < 46)
    return false;
  uint32_t at = 4;
  const uint32_t mime = pm_media::big32(decoded + at);
  at += 4;
  if (!mime || mime > 64 || mime > decodedBytes - at)
    return false;
  at += mime;
  if (decodedBytes - at < 4)
    return false;
  const uint32_t description = pm_media::big32(decoded + at);
  at += 4;
  if (description > decodedBytes - at)
    return false;
  at += description;
  // width, height, depth and indexed colors precede the image length.
  if (decodedBytes - at < 20)
    return false;
  at += 16;
  const uint32_t imageBytes = pm_media::big32(decoded + at);
  at += 4;
  if (!imageBytes)
    return false;
  // Only the picture header is in this prefix; the image itself is streamed.
  if (decodedBytes - at < 4)
    return false;
  ImageFormat format = ImageFormat::None;
  if (decoded[at] == 0xFF && decoded[at + 1] == 0xD8)
    format = ImageFormat::JPEG;
  if (!std::memcmp(decoded + at, "\x89PNG", 4))
    format = ImageFormat::PNG;
  if (format == ImageFormat::None)
    return false;

  const uint64_t imageEnd = uint64_t(at) + uint64_t(imageBytes);
  const uint64_t begin = uint64_t(at / 3) * 4;
  const uint64_t end = ((imageEnd + 2) / 3) * 4;
  if (begin >= length || end > length || end <= begin)
    return false;
  out.coverOffset = offset + static_cast<uint32_t>(begin);
  out.coverLength = static_cast<uint32_t>(end - begin);
  out.coverFormat = format;
  return true;
}

bool comment(PacketCursor &cursor, pm_media::Reader &reader, bool opus, MediaInfo &out) {
  uint8_t id[8];
  if (!cursor.read(id, sizeof(id)))
    return false;
  uint32_t vendor;
  if (opus) {
    if (std::memcmp(id, "OpusTags", 8))
      return false;
    vendor = cursor32(cursor);
  } else {
    if (std::memcmp(id, "\x03vorbis", 7))
      return false;
    vendor = cursor32WithFirst(cursor, id[7]);
  }
  if (!cursor.skip(vendor))
    return false;
  const uint32_t count = cursor32(cursor);
  if (count > kCommentFields)
    return false;
  for (uint32_t i = 0; i < count; ++i) {
    const uint32_t length = cursor32(cursor);
    if (!length)
      continue;
    char key[32] = {};
    uint32_t used = 0;
    bool separator = false;
    uint8_t byte = 0;
    for (; used < length; ++used) {
      if (!cursor.byte(byte))
        return false;
      if (byte == '=') {
        separator = true;
        break;
      }
      if (used < sizeof(key) - 1)
        key[used] = static_cast<char>(byte);
    }
    if (!separator)
      continue;
    const uint32_t value = cursor.position();
    const uint32_t remaining = length - used - 1;
    char *target = nullptr;
    if (keyEquals(key, "TITLE"))
      target = out.title;
    if (keyEquals(key, "ARTIST"))
      target = out.artist;
    if (keyEquals(key, "ALBUM"))
      target = out.album;
    if (target) {
      if (remaining >= sizeof(out.title))
        return false;
      uint8_t bytes[128];
      if (!cursor.read(bytes, remaining))
        return false;
      std::memcpy(target, bytes, remaining);
      target[remaining] = 0;
    } else if (keyEquals(key, "METADATA_BLOCK_PICTURE") && remaining) {
      flacPicture(reader, value, remaining, out);
      if (!cursor.skip(remaining))
        return false;
    } else if (!cursor.skip(remaining)) {
      return false;
    }
  }
  return true;
}

bool lastGranule(pm_media::Reader &reader, uint64_t &granule) {
  uint32_t end = reader.size();
  uint32_t searched = 0;
  while (end && searched < kGranuleSearchBytes) {
    const uint32_t start = end > kGranuleWindowBytes ? end - kGranuleWindowBytes : 0;
    const uint32_t length = end - start;
    uint8_t window[kGranuleWindowBytes];
    if (!reader.readAt(start, window, length))
      return false;
    for (uint32_t i = length; i >= 4; --i) {
      if (std::memcmp(window + i - 4, "OggS", 4))
        continue;
      const uint64_t at = uint64_t(start) + i - 4;
      if (at + 27 > reader.size())
        continue;
      uint8_t header[27];
      if (!reader.readAt(static_cast<uint32_t>(at), header, sizeof(header)) || header[4] ||
          !(header[5] & 0x04))
        continue;
      granule = pm_media::little64(header + 6);
      return true;
    }
    searched += length;
    end = start;
  }
  return false;
}
} // namespace

bool pm_media::oggInfo(Reader &reader, MediaInfo &out) {
  out = {};
  if (reader.size() < 27)
    return false;
  PacketCursor cursor(reader, reader.size());
  if (!cursor.nextPacket())
    return false;
  uint8_t id[8];
  if (!cursor.read(id, sizeof(id)))
    return false;
  bool opus = !std::memcmp(id, "OpusHead", 8);
  if (!opus && std::memcmp(id, "\x01vorbis", 7))
    return false;
  // Both headers carry the rate at byte 12. OpusHead is 19 bytes; Vorbis is 30.
  if (!cursor.skip(4))
    return false;
  uint8_t rateBytes[4];
  if (!cursor.read(rateBytes, sizeof(rateBytes)) || !cursor.skip(opus ? 3 : 14))
    return false;
  uint32_t rate = opus ? 48000 : little32(rateBytes);
  if (!rate)
    return false;
  if (!cursor.nextPacket() || !comment(cursor, reader, opus, out))
    return false;
  uint64_t granule = 0;
  if (!lastGranule(reader, granule))
    return false;
  out.durationMs = granule * 1000 / rate;
  return true;
}
