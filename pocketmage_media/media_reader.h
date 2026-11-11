#pragma once

#include "pocketmage_media.h"

#include <cstddef>

/** Pure parsing boundary used by the SD adapter and in-memory regression fixtures. */
namespace pm_media {
/** Bounded random access; implementations must reject short reads and invalid ranges. */
class Reader {
public:
  virtual ~Reader() = default;
  /** Return the file size in bytes, limited to the SDK stream offset range. */
  virtual uint32_t size() const = 0;
  /** Read count bytes at offset; return false on bounds or I/O failure. */
  virtual bool readAt(uint32_t offset, void *out, size_t count) = 0;
};

/** Parse ID3v2.2/3/4 text and raw JPEG/PNG covers; reject malformed ranges.
 * On success set the post-tag audio offset, even if encoded tag transforms are unsupported.
 * See https://id3.org/id3v2.4.0-structure and https://id3.org/id3v2.3.0
 */
bool readId3(Reader &reader, MediaInfo &out);

/** Validate only the ID3 header/footer and return the first audio byte.
 * Return false on malformed bounds or I/O failure; non-ID3 files start at zero.
 */
bool audioOffset(Reader &reader, uint32_t &out);

/** Position an open SD stream after its leading ID3 tag without scanning frames.
 * Return false on malformed tags or stream I/O failure. The caller retains the slot.
 */
bool skipId3(int slot, uint32_t &out);

/** Sum complete MPEG audio frame durations, including VBR, from offset.
 * Return false with duration zero for unsupported data, truncation or read failure.
 */
bool mp3Duration(Reader &reader, uint32_t offset, uint64_t &duration);
} // namespace pm_media
