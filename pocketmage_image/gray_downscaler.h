#pragma once

#include <cstdint>

// Box-filters grayscale source rows down to the output size, then dithers
// each output row (Floyd-Steinberg) straight into a 1-bit framebuffer
class GrayDownscaler {
public:
  GrayDownscaler() = default;
  ~GrayDownscaler();
  GrayDownscaler(const GrayDownscaler &) = delete;
  GrayDownscaler &operator=(const GrayDownscaler &) = delete;

  // fb:        1bpp framebuffer, MSB first; a set bit is black
  // fbW,fbH:   framebuffer size in pixels
  // boxX,boxY: top-left of a boxSize x boxSize square the image is centred in
  // srcW,srcH: size of the rows that will be pushed
  // fill:      false = fit (longest side = boxSize, letterboxed)
  //            true  = fill (shortest side = boxSize, overflow cropped)
  bool begin(uint8_t *fb, int fbW, int fbH, int boxX, int boxY, int boxSize, int srcW, int srcH,
             bool fill);

  // One source row of srcW gray pixels (0 = black, 255 = white), top to bottom.
  void pushRow(const uint8_t *gray);

  // Emits the last partial output row. Call once after the final pushRow().
  void finish();

private:
  void emit_();

  uint8_t *fb_ = nullptr;
  uint8_t *mem_ = nullptr;
  int fbW_ = 0, fbH_ = 0;
  int outX_ = 0, outY_ = 0, outW_ = 0, outH_ = 0; // scaled image placement
  int clipX0_ = 0, clipY0_ = 0, clipX1_ = 0, clipY1_ = 0; // box, exclusive end
  int srcW_ = 0, srcH_ = 0, srcY_ = 0, curOy_ = 0, rows_ = 0;
  uint32_t *sum_ = nullptr;    // per output column, sum of source pixels
  uint16_t *cols_ = nullptr;   // per output column, source columns feeding it
  int16_t *errCur_ = nullptr;  // outW + 2, offset by one
  int16_t *errNext_ = nullptr; // outW + 2, offset by one
  uint8_t gamma_[256];         // gamma curve applied before dithering
};
