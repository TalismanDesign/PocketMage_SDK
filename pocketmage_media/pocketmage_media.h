#pragma once

#include <pocketmage_image/pocketmage_image.h>

/** Bounded UTF-8 tags and raw embedded-cover region; zero denotes unavailable data. */
struct MediaInfo {
  char title[128];
  char artist[128];
  char album[128];
  uint64_t durationMs;
  uint32_t audioOffset;
  uint32_t coverOffset;
  uint32_t coverLength;
  ImageFormat coverFormat;
};

/** OS-owned streaming metadata service; encoded media never leaves the SD gateway. */
class PocketMageMedia {
public:
  /** Return the shared stateless service. */
  static PocketMageMedia &getInstance();
  /** Probe ID3 tags and MPEG audio duration without loading the file into memory.
   * Copy out only on success. Return false for invalid paths, malformed tags or I/O errors.
   * Unsupported encoded tag transforms leave the affected fields empty.
   */
  bool probe(const char *path, MediaInfo &out);
};

/** Native shared media accessor; ELF apps use generated pm_media_* bindings. */
extern PocketMageMedia &P_MEDIA;
