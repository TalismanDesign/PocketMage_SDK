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

/** Estimate MPEG audio duration from the first frame at or after offset.
 * Use the Xing/Info or VBRI frame count when present; otherwise extrapolate the mean
 * bitrate of the opening frames over the audio bytes, excluding an ID3v1 trailer.
 * Reads a bounded prefix and trailer only. Return false with duration zero when no
 * frame is found or a read fails.
 */
bool mp3Duration(Reader &reader, uint32_t offset, uint64_t &duration);

struct WavFormat {
  uint32_t sampleRate; // Frames per second
  uint32_t dataOffset; // First audio byte in file coordinates
  uint32_t dataBytes;  // Length of the data chunk in bytes
  uint16_t channels;   // Interleaved channel count
  uint16_t bitsPerSample;
  uint16_t blockAlign; // Bytes per frame
  uint16_t codec;      // 1 = PCM integer, 3 = IEEE float
};

/** Walk RIFF/WAVE chunks and resolve the fmt chunk, including WAVE_FORMAT_EXTENSIBLE.
 *
 * See https://www.microsoft.com/en-us/library/windows/desktop/dd390971.aspx
 */
bool wavFormat(Reader &reader, WavFormat &out);

/** Resolve a WAV stream's format and data extent from an open SD slot */
bool wavFormatSlot(int slot, WavFormat &out);

/** Parse RIFF/WAVE fmt, data and LIST/INFO chunks, plus an embedded ID3 chunk.
 *
 * See https://www.rfc-editor.org/rfc/rfc2361 and
 * https://www.microsoft.com/en-us/library/windows/desktop/ee415713.aspx
 */
bool wavInfo(Reader &reader, MediaInfo &out);

/** Parse the first Ogg stream's identification and comment packets and the
 * final page's granule.
 * See https://xiph.org/vorbis/doc/v-comment.html,
 * https://www.rfc-editor.org/rfc/rfc9639 and https://www.rfc-editor.org/rfc/rfc7845
 */
bool oggInfo(Reader &reader, MediaInfo &out);
} // namespace pm_media
