#pragma once

#include <cstdint>

// A byte range of a file read through the SD gateway, so decoders can read
// an image embedded in another file
struct RegionStream {
  int slot;      // gateway stream slot (not a POSIX fd)
  uint32_t base; // offset into the file where the region starts
  uint32_t len;  // length of the region in bytes
  uint32_t pos;  // current read position, relative to base
};

// Region for the next open() (len 0 = rest of file)
void regionSet(uint32_t off, uint32_t len);
void regionClear();

// Opens path and seeks to the region start. Returns nullptr on failure,
// including a region that doesn't fit inside the file.
RegionStream *regionOpen(const char *path);
void regionClose(RegionStream *rs);

// Reads are clamped to the end of the region.
int32_t regionRead(RegionStream *rs, uint8_t *buf, int32_t len);
bool regionSeek(RegionStream *rs, int32_t pos); // pos relative to region start
