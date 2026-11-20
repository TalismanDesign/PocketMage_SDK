#include "region_stream.h"

#include <pocketmage.h>
#include <pocketmage_sd/pocketmage_sd.h>

#include <cstdio>
#include <new>

static uint32_t s_off = 0;
static uint32_t s_len = 0;

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

  auto *rs = new (std::nothrow) RegionStream{slot, s_off, len, 0};
  if (!rs)
    PM_SD().streamClose(slot);
  return rs;
}

void regionClose(RegionStream *rs) {
  if (!rs)
    return;
  PM_SD().streamClose(rs->slot);
  delete rs;
}

int32_t regionRead(RegionStream *rs, uint8_t *buf, int32_t len) {
  if (!rs || len <= 0 || rs->pos >= rs->len)
    return 0;
  uint32_t left = rs->len - rs->pos;
  if ((uint32_t)len > left)
    len = (int32_t)left;
  int32_t n = (int32_t)PM_SD().streamRead(rs->slot, buf, (size_t)len);
  if (n > 0)
    rs->pos += (uint32_t)n;
  return n;
}

bool regionSeek(RegionStream *rs, int32_t pos) {
  if (!rs || pos < 0 || (uint32_t)pos > rs->len)
    return false;
  if (!PM_SD().streamSeek(rs->slot, (long)(rs->base + (uint32_t)pos), SEEK_SET))
    return false;
  rs->pos = (uint32_t)pos;
  return true;
}
