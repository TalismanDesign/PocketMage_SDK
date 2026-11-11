#pragma once

// Piezo buzzer can't reproduce bass (<~500 Hz-1 kHz)
//
// To tune: raise PIEZO_HPF_HZ (800-1000 Hz) if still crunchy; lower toward
// 300 Hz or raise PIEZO_GAIN if too thin/quiet. Keep pwm_audio volume at 0
// dB or below while tuning.

#include <cstddef>
#include <cstdint>

static constexpr float PIEZO_HPF_HZ = 500.0f; // high-pass cutoff
static constexpr float PIEZO_GAIN = 0.6f;     // output headroom

struct Biquad {
  float b0, b1, b2, a1, a2;
  float x1, x2, y1, y2;
};

extern Biquad s_hpf; // shared filter state (defined in piezo_filter.cpp)

// Initialize a 2nd-order Butterworth high-pass filter.
void hpfInit(Biquad &f, float sampleRateHz, float cutoffHz);

// In-place: interleaved int16 PCM with `ch` channels -> filtered mono.
// Returns the new byte count (frames * sizeof(int16_t), mono).
size_t shapeForPiezo(int16_t *pcm, size_t bytes, int ch);
