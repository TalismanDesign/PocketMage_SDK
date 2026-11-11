#pragma once

#include <cstdint>

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
  static PocketMageImage &getInstance();

  // Format and dimensions of [off, off+len) of path (len 0 = rest of file).
  bool probe(const char *path, ImageInfo &out, uint32_t off = 0, uint32_t len = 0);

  // Decode an image file to a 1-bit e-ink buffer with scale + dither.
  // buf is the framebuffer (1bpp, rows packed MSB-first as in PocketMage display).
  // bufW, bufH are framebuffer dimensions in pixels.
  // outX, outY are top-left position in the framebuffer.
  // The image is centred in the maxSize x maxSize box at outX,outY. fill = false
  // fits the longest side to the box; fill = true covers the box with the
  // shortest side and crops the overflow. Images are never upscaled.
  bool decodeTo1Bit(const char *path, uint8_t *buf, int bufW, int bufH, int outX, int outY,
                    int maxSize, bool fill = false);

  // Same, for an image embedded at [off, off+len) of path, e.g. APIC cover
  // art inside an MP3. No extraction to a separate file is needed.
  bool decodeRegionTo1Bit(const char *path, uint32_t off, uint32_t len, uint8_t *buf, int bufW,
                          int bufH, int outX, int outY, int maxSize, bool fill = false);

  // Kept for existing callers; same as decodeTo1Bit.
  bool decodeFromFile(const char *path, uint8_t *buf, int bufW, int bufH, int outX, int outY,
                      int maxSize, bool fill = false);

  // Free resources
  void clear();

private:
  PocketMageImage();
  ~PocketMageImage();
  PocketMageImage(const PocketMageImage &) = delete;
  PocketMageImage &operator=(const PocketMageImage &) = delete;

  ImageInfo _info;
  bool _loaded;
};

extern PocketMageImage &P_IMAGE;
