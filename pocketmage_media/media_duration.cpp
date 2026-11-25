#include "media_reader.h"

#include <cstring>

namespace {
constexpr uint32_t kSyncSearchBytes = 4096;
// Enough frames to average a VBR file's opening bitrate in a few SD reads.
constexpr uint32_t kSampledFrames = 32;
constexpr uint32_t kId3v1Bytes = 128;
constexpr uint32_t kXingFramesFlag = 0x1;
constexpr uint32_t kVbriOffset = 4 + 32;
constexpr uint32_t kMillisecondsPerSecond = 1000;

struct MpegFrame {
  uint32_t bytes, samples, rate, kbps;
  bool layer3, mpeg1, mono;
};

bool mpegFrame(const uint8_t *h, MpegFrame &out) {
  if (h[0] != 0xff || (h[1] & 0xe0) != 0xe0)
    return false;
  const unsigned version = (h[1] >> 3) & 3, layer = (h[1] >> 1) & 3;
  const unsigned bitrate = h[2] >> 4, frequency = (h[2] >> 2) & 3;
  if (version == 1 || !layer || !bitrate || bitrate == 15 || frequency == 3)
    return false;
  // MPEG audio header tables: https://www.mp3-tech.org/programmer/frame_header.html
  constexpr unsigned kRates[] = {44100, 48000, 32000};
  constexpr unsigned kBitrates[5][15] = {
      {0, 32, 64, 96, 128, 160, 192, 224, 256, 288, 320, 352, 384, 416, 448},
      {0, 32, 48, 56, 64, 80, 96, 112, 128, 160, 192, 224, 256, 320, 384},
      {0, 32, 40, 48, 56, 64, 80, 96, 112, 128, 160, 192, 224, 256, 320},
      {0, 32, 48, 56, 64, 80, 96, 112, 128, 144, 160, 176, 192, 224, 256},
      {0, 8, 16, 24, 32, 40, 48, 56, 64, 80, 96, 112, 128, 144, 160}};
  const unsigned table = version == 3 ? 3 - layer : layer == 3 ? 3 : 4;
  out.rate = kRates[frequency] / (version == 3 ? 1 : version == 2 ? 2 : 4);
  out.samples = layer == 3 ? 384 : layer == 1 && version != 3 ? 576 : 1152;
  out.kbps = kBitrates[table][bitrate];
  const unsigned padding = (h[2] >> 1) & 1;
  out.bytes = layer == 3 ? (12 * out.kbps * 1000 / out.rate + padding) * 4
                         : out.samples / 8 * out.kbps * 1000 / out.rate + padding;
  out.layer3 = layer == 1;
  out.mpeg1 = version == 3;
  out.mono = (h[3] >> 6) == 3;
  return true;
}

uint32_t bigEndian32(const uint8_t *p) {
  return uint32_t(p[0]) << 24 | uint32_t(p[1]) << 16 | uint32_t(p[2]) << 8 | p[3];
}

uint64_t framesToMs(uint64_t frames, const MpegFrame &frame) {
  return frames * frame.samples * kMillisecondsPerSecond / frame.rate;
}

// Xing/Info: http://gabriel.mp3-tech.org/mp3infotag.html
// VBRI: https://www.codeproject.com/Articles/8295/MPEG-Audio-Frame-Header#VBRIHeader
bool vbrFrameCount(pm_media::Reader &reader, uint32_t offset, const MpegFrame &frame,
                   uint32_t &frames) {
  if (!frame.layer3)
    return false;
  const uint32_t side = frame.mpeg1 ? (frame.mono ? 17 : 32) : (frame.mono ? 9 : 17);
  uint8_t tag[12];
  const uint32_t xing = offset + 4 + side;
  if (xing + sizeof(tag) <= offset + frame.bytes && reader.readAt(xing, tag, sizeof(tag)) &&
      (!std::memcmp(tag, "Xing", 4) || !std::memcmp(tag, "Info", 4)) &&
      (bigEndian32(tag + 4) & kXingFramesFlag)) {
    frames = bigEndian32(tag + 8);
    return frames > 0;
  }
  constexpr uint32_t kVbriFramesField = 14;
  uint8_t vbri[kVbriFramesField + 4];
  const uint32_t at = offset + kVbriOffset;
  if (at + sizeof(vbri) <= offset + frame.bytes && reader.readAt(at, vbri, sizeof(vbri)) &&
      !std::memcmp(vbri, "VBRI", 4)) {
    frames = bigEndian32(vbri + kVbriFramesField);
    return frames > 0;
  }
  return false;
}

bool firstFrame(pm_media::Reader &reader, uint32_t &offset, MpegFrame &frame) {
  for (uint32_t searched = 0; searched <= kSyncSearchBytes && reader.size() - offset >= 4;
       ++searched, ++offset) {
    uint8_t header[4];
    if (!reader.readAt(offset, header, sizeof(header)))
      return false;
    if (mpegFrame(header, frame) && frame.bytes <= reader.size() - offset)
      return true;
  }
  return false;
}
} // namespace

bool pm_media::mp3Duration(Reader &reader, uint32_t offset, uint64_t &duration) {
  duration = 0;
  if (offset > reader.size())
    return false;
  MpegFrame first{};
  if (!firstFrame(reader, offset, first))
    return false;

  uint32_t vbrFrames = 0;
  if (vbrFrameCount(reader, offset, first, vbrFrames)) {
    duration = framesToMs(vbrFrames, first);
    return true;
  }

  uint32_t end = reader.size();
  uint8_t trailer[3];
  if (end - offset >= kId3v1Bytes && reader.readAt(end - kId3v1Bytes, trailer, sizeof(trailer)) &&
      !std::memcmp(trailer, "TAG", 3))
    end -= kId3v1Bytes;

  // Without a VBR header, extrapolate from the mean bitrate of the opening frames.
  uint64_t sampledBytes = 0, sampledSamples = 0;
  uint32_t at = offset;
  for (uint32_t i = 0; i < kSampledFrames && end - at >= 4; ++i) {
    uint8_t header[4];
    MpegFrame frame{};
    if (!reader.readAt(at, header, sizeof(header)))
      return false;
    if (!mpegFrame(header, frame) || frame.bytes > end - at)
      break;
    sampledBytes += frame.bytes;
    sampledSamples += frame.samples;
    at += frame.bytes;
  }
  if (!sampledBytes)
    return false;
  // A stream keeps one sample rate, so the first frame's rate applies to the whole file.
  duration = sampledSamples * (end - offset) * kMillisecondsPerSecond / (sampledBytes * first.rate);
  return true;
}
