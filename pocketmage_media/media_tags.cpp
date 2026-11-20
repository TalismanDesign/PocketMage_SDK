#include "media_reader.h"

#include <algorithm>
#include <cstring>
#include <limits>

namespace {
constexpr uint32_t kTagHeader = 10;
constexpr uint8_t kFrontCover = 3;

uint32_t be32(const uint8_t *p) {
  return (uint32_t(p[0]) << 24) | (uint32_t(p[1]) << 16) | (uint32_t(p[2]) << 8) | p[3];
}

bool syncsafe(const uint8_t *p, uint32_t &value) {
  if ((p[0] | p[1] | p[2] | p[3]) & 0x80) return false;
  value = (uint32_t(p[0]) << 21) | (uint32_t(p[1]) << 14) | (uint32_t(p[2]) << 7) | p[3];
  return true;
}

class Cursor {
public:
  Cursor(pm_media::Reader &reader, uint32_t start, uint32_t end)
      : reader_(reader), position_(start), end_(end) {}
  bool byte(uint8_t &value) {
    if (position_ == end_) return false;
    if (index_ == buffered_) {
      buffered_ = std::min<size_t>(sizeof(buffer_), end_ - position_);
      if (!reader_.readAt(position_, buffer_, buffered_)) return false;
      index_ = 0;
    }
    value = buffer_[index_++];
    ++position_;
    return true;
  }
  uint32_t position() const { return position_; }
  bool remaining() const { return position_ < end_; }
private:
  pm_media::Reader &reader_;
  uint32_t position_, end_;
  uint8_t buffer_[256];
  size_t index_ = 0, buffered_ = 0;
};

bool unit16(Cursor &cursor, bool big, uint16_t &value) {
  uint8_t a, b;
  if (!cursor.byte(a) || !cursor.byte(b)) return false;
  value = big ? (uint16_t(a) << 8) | b : (uint16_t(b) << 8) | a;
  return true;
}

bool appendUtf8(char *out, size_t capacity, size_t &used, uint32_t code) {
  if (code > 0x10ffff || (code >= 0xd800 && code <= 0xdfff)) code = 0xfffd;
  const size_t count = code < 0x80 ? 1 : code < 0x800 ? 2 : code < 0x10000 ? 3 : 4;
  if (count >= capacity - used) return false;
  if (count == 1) out[used++] = static_cast<char>(code);
  else {
    out[used++] = static_cast<char>((count == 2 ? 0xc0 : count == 3 ? 0xe0 : 0xf0) |
                                    (code >> (6 * (count - 1))));
    for (size_t i = count - 1; i > 0; --i)
      out[used++] = static_cast<char>(0x80 | ((code >> (6 * (i - 1))) & 0x3f));
  }
  out[used] = 0;
  return true;
}

bool text(Cursor &cursor, char *out, size_t capacity) {
  uint8_t encoding;
  if (!cursor.byte(encoding) || encoding > 3) return false;
  size_t used = 0;
  bool big = encoding == 2;
  bool first = true;
  out[0] = 0;
  while (cursor.remaining()) {
    uint32_t code;
    if (encoding == 1 || encoding == 2) {
      uint16_t unit;
      if (!unit16(cursor, big, unit)) return false;
      if (first && encoding == 1 && (unit == 0xfeff || unit == 0xfffe)) {
        big = unit == 0xfffe;
        first = false;
        continue;
      }
      code = unit;
      if (unit >= 0xd800 && unit <= 0xdbff) {
        uint16_t low;
        if (!unit16(cursor, big, low) || low < 0xdc00 || low > 0xdfff) return false;
        code = 0x10000 + ((uint32_t(unit) - 0xd800) << 10) + low - 0xdc00;
      }
    } else {
      uint8_t byte;
      if (!cursor.byte(byte)) return false;
      code = byte;
      if (encoding == 3 && byte >= 0x80) {
        const unsigned count = byte >= 0xc2 && byte <= 0xdf ? 2 :
                               byte >= 0xe0 && byte <= 0xef ? 3 :
                               byte >= 0xf0 && byte <= 0xf4 ? 4 : 0;
        if (!count) return false;
        code = byte & ((1u << (7 - count)) - 1);
        for (unsigned i = 1; i < count; ++i) {
          if (!cursor.byte(byte) || (byte & 0xc0) != 0x80) return false;
          code = (code << 6) | (byte & 0x3f);
        }
        if (code < (count == 2 ? 0x80u : count == 3 ? 0x800u : 0x10000u) ||
            code > 0x10ffff || (code >= 0xd800 && code <= 0xdfff)) return false;
      }
    }
    first = false;
    if (!code || !appendUtf8(out, capacity, used, code)) break;
  }
  return true;
}

bool picture(Cursor &cursor, bool legacy, uint32_t end, MediaInfo &out, bool &front) {
  uint8_t encoding, value, type;
  if (!cursor.byte(encoding) || encoding > 3) return false;
  char mime[32] = {};
  size_t used = 0;
  if (legacy) {
    for (int i = 0; i < 3; ++i) {
      if (!cursor.byte(value)) return false;
      mime[used++] = static_cast<char>(value);
    }
  } else {
    do {
      if (!cursor.byte(value)) return false;
      if (used < sizeof(mime) - 1) mime[used++] = static_cast<char>(value);
    } while (value);
  }
  if (!cursor.byte(type)) return false;
  do {
    uint8_t second = 0;
    if (!cursor.byte(value) || ((encoding == 1 || encoding == 2) && !cursor.byte(second)))
      return false;
    if (!value && !second) break;
  } while (true);
  ImageFormat format = ImageFormat::None;
  if (!std::strcmp(mime, "image/jpeg") || !std::strcmp(mime, "image/jpg") ||
      !std::strcmp(mime, "JPG")) format = ImageFormat::JPEG;
  if (!std::strcmp(mime, "image/png") || !std::strcmp(mime, "PNG")) format = ImageFormat::PNG;
  if (format != ImageFormat::None && cursor.position() < end &&
      (!out.coverLength || (type == kFrontCover && !front))) {
    out.coverOffset = cursor.position();
    out.coverLength = end - out.coverOffset;
    out.coverFormat = format;
    front = type == kFrontCover;
  }
  return true;
}

bool frame(pm_media::Reader &reader, const char *id, uint32_t body, uint32_t end,
           MediaInfo &out, bool &front) {
  Cursor cursor(reader, body, end);
  if (!std::strcmp(id, "APIC") || !std::strcmp(id, "PIC"))
    return picture(cursor, !std::strcmp(id, "PIC"), end, out, front);
  char *target = nullptr;
  if (!std::strcmp(id, "TIT2") || !std::strcmp(id, "TT2")) target = out.title;
  if (!std::strcmp(id, "TPE1") || !std::strcmp(id, "TP1")) target = out.artist;
  if (!std::strcmp(id, "TALB") || !std::strcmp(id, "TAL")) target = out.album;
  if (target) return text(cursor, target, sizeof(out.title));
  if (!std::strcmp(id, "TLEN") || !std::strcmp(id, "TLE")) {
    char digits[32] = {};
    if (!text(cursor, digits, sizeof(digits))) return false;
    uint64_t duration = 0;
    for (const char *p = digits; *p; ++p) {
      if (*p < '0' || *p > '9' || duration > (std::numeric_limits<uint64_t>::max() - 9) / 10)
        return false;
      duration = duration * 10 + (*p - '0');
    }
    out.durationMs = duration;
  }
  return true;
}
}

namespace {
bool tagBounds(pm_media::Reader &reader, uint8_t (&header)[kTagHeader], uint32_t &end,
               uint32_t &audioOffset) {
  end = audioOffset = 0;
  if (reader.size() < kTagHeader) {
    if (reader.size() && !reader.readAt(0, header, reader.size())) return false;
    return reader.size() < 3 || std::memcmp(header, "ID3", 3);
  }
  if (!reader.readAt(0, header, sizeof(header))) return false;
  if (std::memcmp(header, "ID3", 3)) return true;
  const uint8_t version = header[3];
  uint32_t tagSize;
  if (!syncsafe(header + 6, tagSize) || tagSize > reader.size() - kTagHeader) return false;
  end = kTagHeader + tagSize;
  audioOffset = end;
  if (version == 4 && (header[5] & 0x10)) {
    uint8_t footer[kTagHeader];
    if (reader.size() - end < kTagHeader || !reader.readAt(end, footer, sizeof(footer)) ||
        std::memcmp(footer, "3DI", 3) || std::memcmp(footer + 3, header + 3, 7)) return false;
    audioOffset += kTagHeader;
  }
  return true;
}
}

bool pm_media::audioOffset(Reader &reader, uint32_t &out) {
  uint8_t header[kTagHeader] = {};
  uint32_t end, offset;
  if (!tagBounds(reader, header, end, offset)) return false;
  out = offset;
  return true;
}

bool pm_media::readId3(Reader &reader, MediaInfo &out) {
  out = {};
  uint8_t header[kTagHeader] = {};
  uint32_t end;
  if (!tagBounds(reader, header, end, out.audioOffset)) return false;
  if (!end) return true;
  const uint8_t version = header[3];
  // Raw-region covers cannot reference de-unsynchronised or compressed bytes.
  if (version < 2 || version > 4 || (header[5] & 0x80) ||
      (version == 2 && (header[5] & 0x40))) return true;
  uint32_t position = kTagHeader;
  if (header[5] & 0x40) {
    uint8_t size[4];
    uint32_t extended;
    if (end - position < sizeof(size) || !reader.readAt(position, size, sizeof(size))) return false;
    if (version == 4) {
      if (!syncsafe(size, extended) || extended < 6) return false;
    } else {
      extended = be32(size);
      if (extended > end - position - sizeof(size) || extended < 6) return false;
      extended += sizeof(size);
    }
    if (extended > end - position) return false;
    position += extended;
  }
  bool front = false;
  const uint32_t frameHeader = version == 2 ? 6 : 10;
  const unsigned idLength = version == 2 ? 3 : 4;
  while (end - position >= frameHeader) {
    if (!reader.readAt(position, header, frameHeader)) return false;
    if (!header[0]) break;
    char id[5] = {};
    for (unsigned i = 0; i < idLength; ++i) {
      if (!((header[i] >= 'A' && header[i] <= 'Z') || (header[i] >= '0' && header[i] <= '9')))
        return false;
      id[i] = static_cast<char>(header[i]);
    }
    uint32_t size;
    if (version == 2) size = (uint32_t(header[3]) << 16) | (uint32_t(header[4]) << 8) | header[5];
    else if (version == 4) { if (!syncsafe(header + 4, size)) return false; }
    else size = be32(header + 4);
    if (!size || size > end - position - frameHeader) return false;
    uint32_t body = position + frameHeader;
    const uint32_t next = body + size;
    const uint8_t flags = version == 2 ? 0 : header[9];
    const bool unsupported = version == 4 ? (flags & 0xbe) : (flags & 0xdf);
    const uint32_t extra = version == 4 ? ((flags & 0x40) ? 1 : 0) + ((flags & 1) ? 4 : 0)
                                        : ((flags & 0x20) ? 1 : 0);
    if (!unsupported) {
      if (extra >= size || !frame(reader, id, body + extra, next, out, front)) return false;
    }
    position = next;
  }
  return true;
}
