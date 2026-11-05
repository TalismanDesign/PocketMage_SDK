#include "piezo_filter.h"

#include <cmath>

// Piezo high-pass filter state, shared across the decode task.
Biquad s_hpf;

void hpfInit(Biquad &f, float sampleRateHz, float cutoffHz) {
  const float q = 0.7071f;
  const float w0 = 2.0f * (float)M_PI * cutoffHz / sampleRateHz;
  const float c = cosf(w0);
  const float alpha = sinf(w0) / (2.0f * q);
  const float a0 = 1.0f + alpha;

  f.b0 = (1.0f + c) / 2.0f / a0;
  f.b1 = -(1.0f + c) / a0;
  f.b2 = f.b0;
  f.a1 = -2.0f * c / a0;
  f.a2 = (1.0f - alpha) / a0;

  f.x1 = f.x2 = f.y1 = f.y2 = 0;
}

// In-place: interleaved int16 PCM with `ch` channels -> filtered mono.
// Returns the new byte count (frames * sizeof(int16_t), mono).
size_t shapeForPiezo(int16_t *pcm, size_t bytes, int ch) {
  if (!pcm || ch < 1)
    return 0;

  Biquad *f = &s_hpf;

  size_t frames = bytes / (sizeof(int16_t) * ch);
  for (size_t i = 0; i < frames; ++i) {
    float x = (float)pcm[i * ch];
    if (ch == 2)
      x = (x + (float)pcm[i * ch + 1]) * 0.5f;

    float y = f->b0 * x + f->b1 * f->x1 + f->b2 * f->x2 - f->a1 * f->y1 - f->a2 * f->y2;

    f->x2 = f->x1;
    f->x1 = x;
    f->y2 = f->y1;
    f->y1 = y;

    y *= PIEZO_GAIN;
    if (y > 32767.0f)
      y = 32767.0f;
    if (y < -32768.0f)
      y = -32768.0f;

    pcm[i] = (int16_t)y;
  }

  return frames * sizeof(int16_t);
}
