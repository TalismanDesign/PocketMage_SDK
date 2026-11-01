#pragma once

#include <Arduino.h>
#include <FS.h>

#include <functional>

// Image format probe result
enum class ImageFormat {
  None,
  JPEG,
  PNG,
};

// Decoded image metadata
struct ImageInfo {
  int width;
  int height;
  ImageFormat format;
};

class PocketMageImage {
 public:
  static PocketMageImage& getInstance();

  // Probe a file (works on SD paths; uses global_fs). Does not extract APIC.
  bool probe(const char* path, ImageInfo& out);

  // Decode image to 1-bit e-ink buffer with dither + scale.
  // buf is the framebuffer (1bpp, rows packed LSB-first as in PocketMage display).
  // bufW, bufH are framebuffer dimensions in pixels.
  // outX, outY are top-left position in the framebuffer.
  // maxSize is the target bounding box (longest side) for scaling.
  bool decodeTo1Bit(const char* path, uint8_t* buf, int bufW, int bufH,
                    int outX, int outY, int maxSize);

  // Decode from already-extracted JPEG/PNG bytes (for APIC cache file).
  bool decodeFromFile(const char* path, uint8_t* buf, int bufW, int bufH,
                      int outX, int outY, int maxSize);

  // Free resources
  void clear();

 private:
  PocketMageImage();
  ~PocketMageImage();
  PocketMageImage(const PocketMageImage&) = delete;
  PocketMageImage& operator=(const PocketMageImage&) = delete;

  bool probeFile_(File& f, ImageInfo& out);
  bool decodeJPEG_(const char* path, uint8_t* buf, int bufW, int bufH,
                   int outX, int outY, int maxSize);
  bool decodePNG_(const char* path, uint8_t* buf, int bufW, int bufH,
                  int outX, int outY, int maxSize);

  ImageInfo _info;
  bool _loaded;
};

extern PocketMageImage& P_IMAGE;
