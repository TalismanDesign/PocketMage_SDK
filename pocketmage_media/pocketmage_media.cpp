#include "pocketmage_media.h"
#include "media_reader.h"

#include <pocketmage.h>

#include <algorithm>
#include <climits>
#include <cstring>

namespace {
class SdReader : public pm_media::Reader {
public:
  SdReader(int slot, uint32_t size) : slot_(slot), size_(size) {
  }
  uint32_t size() const override {
    return size_;
  }
  bool readAt(uint32_t offset, void *out, size_t count) override {
    if (offset > size_ || count > size_ - offset || (!out && count))
      return false;
    auto *destination = static_cast<uint8_t *>(out);
    while (count) {
      if (offset < base_ || offset - base_ >= buffered_) {
        base_ = offset;
        buffered_ = std::min<size_t>(sizeof(buffer_), size_ - offset);
        if (offset > LONG_MAX || !PM_SD().streamSeek(slot_, static_cast<long>(offset), SEEK_SET) ||
            PM_SD().streamRead(slot_, buffer_, buffered_) != buffered_) {
          failed_ = true;
          buffered_ = 0;
          return false;
        }
      }
      const size_t available = std::min<size_t>(count, buffered_ - (offset - base_));
      std::memcpy(destination, buffer_ + (offset - base_), available);
      destination += available;
      offset += available;
      count -= available;
    }
    return true;
  }
  bool failed() const {
    return failed_;
  }

private:
  int slot_;
  uint32_t size_, base_ = 0;
  size_t buffered_ = 0;
  bool failed_ = false;
  uint8_t buffer_[2048];
};
} // namespace

PocketMageMedia &PocketMageMedia::getInstance() {
  static PocketMageMedia instance;
  return instance;
}

PocketMageMedia &P_MEDIA = PocketMageMedia::getInstance();

bool pm_media::skipId3(int slot, uint32_t &out) {
  const size_t size = PM_SD().streamSize(slot);
  if (size > LONG_MAX)
    return false;
  SdReader reader(slot, static_cast<uint32_t>(size));
  uint32_t offset;
  if (!audioOffset(reader, offset) ||
      !PM_SD().streamSeek(slot, static_cast<long>(offset), SEEK_SET))
    return false;
  out = offset;
  return true;
}

bool pm_media::wavFormatSlot(int slot, WavFormat &out) {
  const size_t size = PM_SD().streamSize(slot);
  if (size > LONG_MAX)
    return false;
  SdReader reader(slot, static_cast<uint32_t>(size));
  WavFormat format{};
  if (!pm_media::wavFormat(reader, format))
    return false;
  out = format;
  return true;
}

bool PocketMageMedia::probe(const char *path, MediaInfo &out) {
  if (!path || !path[0])
    return false;
  int slot = PM_SD().streamOpen(path, 'r');
  if (slot < 0)
    return false;
  const size_t size = PM_SD().streamSize(slot);
  MediaInfo info{};
  bool valid = size <= LONG_MAX;
  if (valid) {
    SdReader reader(slot, static_cast<uint32_t>(size));
    uint8_t magic[12] = {};
    const size_t prefix = std::min<size_t>(sizeof(magic), size);
    valid = prefix >= 4 && reader.readAt(0, magic, prefix);
    if (valid && prefix >= 12 && !std::memcmp(magic, "RIFF", 4) &&
        !std::memcmp(magic + 8, "WAVE", 4)) {
      valid = pm_media::wavInfo(reader, info);
    } else if (valid && !std::memcmp(magic, "OggS", 4)) {
      valid = pm_media::oggInfo(reader, info);
    } else if (valid) {
      valid = pm_media::readId3(reader, info);
      if (valid && !info.durationMs) {
        // A non-MPEG file or unknown duration still has useful ID3 metadata.
        if (!pm_media::mp3Duration(reader, info.audioOffset, info.durationMs))
          info.durationMs = 0;
      }
    }
    if (valid)
      valid = !reader.failed();
  }
  if (!PM_SD().streamClose(slot))
    valid = false;
  if (valid)
    out = info;
  return valid;
}
