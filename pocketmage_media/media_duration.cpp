#include "media_reader.h"

#include <pocketmage_audio/audio_pcm.h>

#include <cstring>

namespace {
struct MpegFrame {
  uint32_t bytes, samples, rate;
};

bool mpegFrame(const uint8_t *h, MpegFrame &out) {
  if (h[0] != 0xff || (h[1] & 0xe0) != 0xe0) return false;
  const unsigned version = (h[1] >> 3) & 3, layer = (h[1] >> 1) & 3;
  const unsigned bitrate = h[2] >> 4, frequency = (h[2] >> 2) & 3;
  if (version == 1 || !layer || !bitrate || bitrate == 15 || frequency == 3) return false;
  // MPEG audio header tables: https://www.mp3-tech.org/programmer/frame_header.html
  constexpr unsigned kRates[] = {44100, 48000, 32000};
  constexpr unsigned kBitrates[5][15] = {
      {0,32,64,96,128,160,192,224,256,288,320,352,384,416,448},
      {0,32,48,56,64,80,96,112,128,160,192,224,256,320,384},
      {0,32,40,48,56,64,80,96,112,128,160,192,224,256,320},
      {0,32,48,56,64,80,96,112,128,144,160,176,192,224,256},
      {0,8,16,24,32,40,48,56,64,80,96,112,128,144,160}};
  const unsigned table = version == 3 ? 3 - layer : layer == 3 ? 3 : 4;
  out.rate = kRates[frequency] / (version == 3 ? 1 : version == 2 ? 2 : 4);
  out.samples = layer == 3 ? 384 : layer == 1 && version != 3 ? 576 : 1152;
  const unsigned padding = (h[2] >> 1) & 1;
  out.bytes = layer == 3 ? (12 * kBitrates[table][bitrate] * 1000 / out.rate + padding) * 4
                         : out.samples / 8 * kBitrates[table][bitrate] * 1000 / out.rate + padding;
  return true;
}
}

bool pm_media::mp3Duration(Reader &reader, uint32_t offset, uint64_t &duration) {
  duration = 0;
  if (offset > reader.size()) return false;
  pm_audio::PlaybackClock clock;
  bool started = false;
  constexpr uint32_t kSyncSearchBytes = 4096;
  uint32_t searched = 0;
  while (reader.size() - offset >= 4) {
    uint8_t header[4];
    if (!reader.readAt(offset, header, sizeof(header))) return false;
    // ID3v1 is a trailer, not another MPEG frame.
    if (reader.size() - offset == 128 && !std::memcmp(header, "TAG", 3)) break;
    MpegFrame frame;
    if (!mpegFrame(header, frame)) {
      if (started || searched++ == kSyncSearchBytes) return false;
      ++offset;
      continue;
    }
    if (frame.bytes > reader.size() - offset ||
        !clock.advance(frame.samples * sizeof(int16_t), 1, frame.rate)) return false;
    started = true;
    offset += frame.bytes;
  }
  if (!started) return false;
  duration = clock.milliseconds();
  return true;
}
