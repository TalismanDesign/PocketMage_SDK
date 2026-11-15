#include "gray_downscaler.h"

#include <cmath>
#include <cstdlib>
#include <cstring>

GrayDownscaler::~GrayDownscaler() { free(mem_); }

bool GrayDownscaler::begin(uint8_t *fb, int fbW, int fbH, int boxX, int boxY, int boxSize,
                           int srcW, int srcH, bool fill) {
  if (!fb || fbW <= 0 || fbH <= 0 || srcW <= 0 || srcH <= 0 || boxSize <= 0)
    return false;

  int longest = srcW > srcH ? srcW : srcH;
  int shortest = srcW < srcH ? srcW : srcH;
  float scale = (float)boxSize / (fill ? shortest : longest);
  if (scale > 1.0f)
    scale = 1.0f; // no upscaling: every output pixel needs a source pixel
  outW_ = (int)(srcW * scale + 0.5f);
  outH_ = (int)(srcH * scale + 0.5f);
  if (outW_ < 1)
    outW_ = 1;
  if (outH_ < 1)
    outH_ = 1;
  if (outW_ > srcW)
    outW_ = srcW;
  if (outH_ > srcH)
    outH_ = srcH;

  // Centre in the box; with fill, the overflow lands outside the clip.
  outX_ = boxX + (boxSize - outW_) / 2;
  outY_ = boxY + (boxSize - outH_) / 2;
  clipX0_ = boxX;
  clipY0_ = boxY;
  clipX1_ = boxX + boxSize;
  clipY1_ = boxY + boxSize;

  fb_ = fb;
  fbW_ = fbW;
  fbH_ = fbH;
  srcW_ = srcW;
  srcH_ = srcH;
  srcY_ = curOy_ = rows_ = 0;

  free(mem_);
  size_t bytes = outW_ * (sizeof(uint32_t) + sizeof(uint16_t)) + 2 * (outW_ + 2) * sizeof(int16_t);
  mem_ = (uint8_t *)calloc(1, bytes);
  if (!mem_)
    return false;
  sum_ = (uint32_t *)mem_;
  cols_ = (uint16_t *)(sum_ + outW_);
  errCur_ = (int16_t *)(cols_ + outW_);
  errNext_ = errCur_ + outW_ + 2;

  // outW <= srcW, so every output column gets at least one source column.
  for (int x = 0; x < srcW_; x++)
    cols_[x * outW_ / srcW_]++;

  // Darken midtones before dithering; dithered photos read light on e-ink
  // because white paper dominates. 1.0 = unchanged, higher = darker.
  static constexpr float kGamma = 1.8f;
  for (int i = 0; i < 256; i++)
    gamma_[i] = (uint8_t)(255.0f * powf(i / 255.0f, kGamma) + 0.5f);

  return true;
}

void GrayDownscaler::pushRow(const uint8_t *gray) {
  if (!mem_ || !gray || srcY_ >= srcH_)
    return;
  int oy = srcY_++ * outH_ / srcH_;
  if (rows_ && oy != curOy_)
    emit_();
  curOy_ = oy;
  for (int x = 0; x < srcW_; x++)
    sum_[x * outW_ / srcW_] += gray[x];
  rows_++;
}

void GrayDownscaler::finish() {
  if (mem_ && rows_)
    emit_();
}

void GrayDownscaler::emit_() {
  int rowBytes = (fbW_ + 7) / 8;
  int y = outY_ + curOy_;
  bool rowVisible = y >= clipY0_ && y < clipY1_ && y >= 0 && y < fbH_;

  // Dither every column, visible or not, so the error flow matches the
  // full image and the crop edge doesn't show a seam.
  for (int ox = 0; ox < outW_; ox++) {
    int avg = (int)(sum_[ox] / ((uint32_t)cols_[ox] * rows_));
    int v = gamma_[avg] + errCur_[ox + 1];
    v = v < 0 ? 0 : (v > 255 ? 255 : v);
    bool white = v > 127;
    int e = v - (white ? 255 : 0);
    errCur_[ox + 2] += e * 7 / 16;
    errNext_[ox] += e * 3 / 16;
    errNext_[ox + 1] += e * 5 / 16;
    errNext_[ox + 2] += e * 1 / 16;

    int x = outX_ + ox;
    if (!rowVisible || x < clipX0_ || x >= clipX1_ || x < 0 || x >= fbW_)
      continue;
    uint8_t m = 0x80 >> (x & 7);
    uint8_t &b = fb_[y * rowBytes + (x >> 3)];
    if (white)
      b &= ~m;
    else
      b |= m;
  }

  int16_t *t = errCur_;
  errCur_ = errNext_;
  errNext_ = t;
  memset(errNext_, 0, (outW_ + 2) * sizeof(int16_t));
  memset(sum_, 0, outW_ * sizeof(uint32_t));
  rows_ = 0;
}
