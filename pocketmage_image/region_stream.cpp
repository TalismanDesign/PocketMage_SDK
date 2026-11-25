#include "region_stream.h"

#include <pocketmage.h>
#include <pocketmage_sd/pocketmage_sd.h>

#include <algorithm>
#include <cstring>
#include <new>

static uint32_t s_off = 0;
static uint32_t s_len = 0;

namespace {
constexpr uint32_t kProbeChars = 8;

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

bool decodeGroup(const uint8_t *chars, uint8_t *out) {
  const int a = base64Value(chars[0]), b = base64Value(chars[1]);
  if (a < 0 || b < 0)
    return false;
  const int c = chars[2] == '=' ? 0 : base64Value(chars[2]);
  const int d = chars[3] == '=' ? 0 : base64Value(chars[3]);
  if (c < 0 || d < 0 || (chars[2] == '=' && chars[3] != '='))
    return false;
  out[0] = static_cast<uint8_t>((a << 2) | (b >> 4));
  out[1] = static_cast<uint8_t>((b << 4) | (c >> 2));
  out[2] = static_cast<uint8_t>((c << 6) | d);
  return true;
}

bool base64Region(int slot, uint32_t off, uint32_t len, uint32_t &decodedLen, uint32_t &encodedSkip,
                  uint32_t &skip) {
  if (len < kProbeChars || (len & 3))
    return false;
  uint8_t head[kProbeChars];
  if (!PM_SD().streamSeek(slot, static_cast<long>(off), SEEK_SET) ||
      PM_SD().streamRead(slot, head, sizeof(head)) != sizeof(head))
    return false;
  uint8_t probe[6];
  if (!decodeGroup(head, probe) || !decodeGroup(head + 4, probe + 3))
    return false;

  uint32_t start = sizeof(probe);
  for (uint32_t i = 0; i + 1 < sizeof(probe); ++i) {
    if (probe[i] == 0xFF && probe[i + 1] == 0xD8) {
      start = i;
      break;
    }
  }
  if (start == sizeof(probe)) {
    for (uint32_t i = 0; i + 3 < sizeof(probe); ++i) {
      if (probe[i] == 0x89 && probe[i + 1] == 'P' && probe[i + 2] == 'N' && probe[i + 3] == 'G') {
        start = i;
        break;
      }
    }
  }
  if (start == sizeof(probe))
    return false;
  encodedSkip = start / 3 * 4;
  skip = start % 3;

  uint8_t tail[2];
  if (!PM_SD().streamSeek(slot, static_cast<long>(off + len - 2), SEEK_SET) ||
      PM_SD().streamRead(slot, tail, sizeof(tail)) != sizeof(tail))
    return false;
  decodedLen = (len - encodedSkip) / 4 * 3;
  if (tail[0] == '=')
    decodedLen -= 2;
  else if (tail[1] == '=')
    decodedLen -= 1;
  if (skip > decodedLen)
    return false;
  decodedLen -= skip;
  return decodedLen >= 4;
}
} // namespace

void regionSet(uint32_t off, uint32_t len) {
  s_off = off;
  s_len = len;
}

void regionClear() {
  s_off = 0;
  s_len = 0;
}

RegionStream *regionOpen(const char *path) {
  int slot = PM_SD().streamOpen(path, 'r');
  if (slot < 0)
    return nullptr;

  uint32_t fsz = (uint32_t)PM_SD().streamSize(slot);
  if (s_off > fsz) {
    PM_SD().streamClose(slot);
    return nullptr;
  }
  uint32_t len = s_len ? s_len : fsz - s_off;
  if (len > fsz - s_off || !PM_SD().streamSeek(slot, (long)s_off, SEEK_SET)) {
    PM_SD().streamClose(slot);
    return nullptr;
  }

  uint32_t decodedLen = 0, encodedSkip = 0, skip = 0;
  const bool base64 = base64Region(slot, s_off, len, decodedLen, encodedSkip, skip);
  const uint32_t regionBase = s_off + encodedSkip;
  if (!PM_SD().streamSeek(slot, (long)regionBase, SEEK_SET)) {
    PM_SD().streamClose(slot);
    return nullptr;
  }

  auto *rs = new (std::nothrow)
      RegionStream{slot, regionBase, base64 ? decodedLen : len,        0,  base64,
                   skip, 0,          base64 ? len - encodedSkip : len, {}, 0,
                   0};
  if (!rs) {
    PM_SD().streamClose(slot);
    return nullptr;
  }
  if (base64 && !regionSeek(rs, 0)) {
    regionClose(rs);
    return nullptr;
  }
  return rs;
}

void regionClose(RegionStream *rs) {
  if (!rs)
    return;
  PM_SD().streamClose(rs->slot);
  delete rs;
}

int32_t regionRead(RegionStream *rs, uint8_t *buf, int32_t len) {
  if (!rs || !buf || len <= 0 || rs->pos >= rs->len)
    return 0;
  uint32_t left = rs->len - rs->pos;
  if ((uint32_t)len > left)
    len = static_cast<int32_t>(left);

  if (!rs->base64) {
    int32_t n = static_cast<int32_t>(PM_SD().streamRead(rs->slot, buf, static_cast<size_t>(len)));
    if (n > 0)
      rs->pos += static_cast<uint32_t>(n);
    return n;
  }

  uint8_t *out = buf;
  int32_t done = 0;
  while (done < len) {
    if (!rs->decodedAvail) {
      if (rs->encodedPos + 4 > rs->encodedLen)
        break;
      uint8_t chars[4];
      if (PM_SD().streamRead(rs->slot, chars, sizeof(chars)) != sizeof(chars) ||
          !decodeGroup(chars, rs->decoded))
        break;
      rs->encodedPos += 4;
      rs->decodedOffset = 0;
      rs->decodedAvail = 3;
    }
    const uint32_t take = std::min<uint32_t>(rs->decodedAvail, static_cast<uint32_t>(len - done));
    std::memcpy(out + done, rs->decoded + rs->decodedOffset, take);
    rs->decodedOffset += take;
    rs->decodedAvail -= take;
    rs->pos += take;
    done += static_cast<int32_t>(take);
  }
  return done;
}

bool regionSeek(RegionStream *rs, int32_t pos) {
  if (!rs || pos < 0 || (uint32_t)pos > rs->len)
    return false;
  if (!rs->base64) {
    if (!PM_SD().streamSeek(rs->slot, (long)(rs->base + (uint32_t)pos), SEEK_SET))
      return false;
    rs->pos = (uint32_t)pos;
    return true;
  }

  const uint32_t target = static_cast<uint32_t>(pos);
  if (target == rs->len) {
    rs->pos = target;
    rs->encodedPos = 0;
    rs->decodedAvail = 0;
    return true;
  }
  const uint32_t absolute = rs->skip + target;
  const uint32_t encoded = absolute / 3 * 4;
  if (encoded > rs->encodedLen ||
      !PM_SD().streamSeek(rs->slot, (long)(rs->base + encoded), SEEK_SET))
    return false;
  rs->encodedPos = encoded;
  rs->decodedOffset = 0;
  rs->decodedAvail = 0;
  rs->pos = target;

  uint8_t chars[4];
  if (PM_SD().streamRead(rs->slot, chars, sizeof(chars)) != sizeof(chars) ||
      !decodeGroup(chars, rs->decoded))
    return false;
  rs->encodedPos += 4;
  rs->decodedOffset = absolute % 3;
  rs->decodedAvail = 3 - rs->decodedOffset;
  return true;
}
