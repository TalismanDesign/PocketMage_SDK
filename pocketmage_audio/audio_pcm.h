#pragma once

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <limits>

/** PCM operations shared by every audio sink, independent of transport and tags. */
namespace pm_audio {

/** Scale signed 16-bit PCM in place using the legacy -16..16 linear gain.
 * Clamp overflow to the PCM range. Return false for invalid gain, buffer or size.
 * memcpy permits decoder buffers that are not aligned to an int16_t boundary.
 */
inline bool scalePcm16(void *buffer, size_t bytes, int8_t gain) {
  constexpr int kUnity = 16;
  if ((!buffer && bytes) || bytes % sizeof(int16_t) || gain < -kUnity || gain > kUnity)
    return false;
  auto *data = static_cast<uint8_t *>(buffer);
  for (size_t offset = 0; offset < bytes; offset += sizeof(int16_t)) {
    int16_t sample;
    std::memcpy(&sample, data + offset, sizeof(sample));
    int32_t scaled = static_cast<int32_t>(sample) * (gain + kUnity) / kUnity;
    if (scaled < std::numeric_limits<int16_t>::min())
      scaled = std::numeric_limits<int16_t>::min();
    if (scaled > std::numeric_limits<int16_t>::max())
      scaled = std::numeric_limits<int16_t>::max();
    sample = static_cast<int16_t>(scaled);
    std::memcpy(data + offset, &sample, sizeof(sample));
  }
  return true;
}

/** Track accepted PCM duration without bitrate estimates or pause-time drift. */
class PlaybackClock {
public:
  /** Clear the duration and fractional sample accumulator for a new track. */
  void reset() { micros_ = remainder_ = rate_ = 0; }

  /** Add complete 16-bit PCM frames at rate Hz; reject invalid data or overflow.
   * Channel counts are limited to the decoder's supported mono/stereo outputs.
   * A rate change discards less than one microsecond of fractional time.
   */
  bool advance(size_t bytes, unsigned channels, uint32_t rate) {
    if ((channels != 1 && channels != 2) || !rate || bytes % (channels * sizeof(int16_t)))
      return false;
    constexpr uint64_t kMicrosPerSecond = 1000000;
    constexpr uint64_t kMaximum = std::numeric_limits<uint64_t>::max();
    const uint64_t frames = bytes / (channels * sizeof(int16_t));
    const uint64_t remainder = rate == rate_ ? remainder_ : 0;
    if (frames > (kMaximum - remainder) / kMicrosPerSecond)
      return false;
    const uint64_t scaled = frames * kMicrosPerSecond + remainder;
    const uint64_t delta = scaled / rate;
    if (delta > kMaximum - micros_)
      return false;
    micros_ += delta;
    remainder_ = scaled % rate;
    rate_ = rate;
    return true;
  }

  /** Return accepted PCM milliseconds; transport buffering may lead audible time. */
  uint64_t milliseconds() const { return micros_ / 1000; }

private:
  uint64_t micros_ = 0;
  uint64_t remainder_ = 0;
  uint32_t rate_ = 0;
};

/** Protect a new track's timeline from a superseded decoder's final write.
 * The audio service mutex serializes requests, advances, and queries.
 */
class PlaybackTimeline {
public:
  /** Reset duration for a newly accepted nonzero playback request id. */
  void request(uint32_t id) { id_ = id; clock_.reset(); }
  /** Ignore stale PCM; return false only for invalid current-request timing. */
  bool advance(uint32_t id, size_t bytes, unsigned channels, uint32_t rate) {
    return id != id_ || clock_.advance(bytes, channels, rate);
  }
  /** Return the current request's accepted PCM duration in milliseconds. */
  uint64_t milliseconds() const { return clock_.milliseconds(); }

private:
  uint32_t id_ = 0;
  PlaybackClock clock_;
};
} // namespace pm_audio
