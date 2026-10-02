// Small DSP helpers: 16 kHz <-> 8 kHz conversion for Codec2, volume with output ceiling, siren tones.
// Portable, host-tested. SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <stddef.h>
#include <stdint.h>

namespace lr {

inline int16_t sat16(int32_t v) { return v > 32767 ? 32767 : (v < -32768 ? -32768 : static_cast<int16_t>(v)); }

// Linear gain per volume level in Q15, level 0 = mute. The ceiling (AUDIO_CEILING_Q15 in config.h) is
// folded into the gain so that no level can exceed the speaker limit.
inline int16_t volume_gain_q15(int level, int levels, int16_t ceiling_q15) {
  static const int16_t table[6] = {0, 2000, 4500, 9000, 17000, 32767};  // ~ -24 .. 0 dB
  if (level <= 0) return 0;
  if (level > levels - 1) level = levels - 1;
  int idx = level * 5 / (levels - 1);
  return static_cast<int16_t>((static_cast<int32_t>(table[idx]) * ceiling_q15) >> 15);
}
inline int16_t apply_gain(int16_t s, int16_t gain_q15) { return sat16((static_cast<int32_t>(s) * gain_q15) >> 15); }

// 16 kHz -> 8 kHz: 7-tap symmetric low-pass then drop every other sample. in: 2*n_out samples.
class Decimator2 {
 public:
  void process(const int16_t* in, int16_t* out, size_t n_out) {
    static const int32_t h[7] = {-2, 0, 10, 16, 10, 0, -2};  // sum = 32 -> >> 5
    for (size_t i = 0; i < n_out; i++) {
      for (int k = 0; k < 2; k++) {
        for (int j = 0; j < 6; j++) hist_[j] = hist_[j + 1];
        hist_[6] = in[2 * i + k];
      }
      int32_t acc = 0;
      for (int j = 0; j < 7; j++) acc += h[j] * hist_[j];
      out[i] = sat16(acc >> 5);
    }
  }

 private:
  int16_t hist_[7] = {0, 0, 0, 0, 0, 0, 0};
};

// 8 kHz -> 16 kHz by linear interpolation. in: n_in samples, out: 2*n_in.
class Interpolator2 {
 public:
  void process(const int16_t* in, int16_t* out, size_t n_in) {
    for (size_t i = 0; i < n_in; i++) {
      out[2 * i] = sat16((static_cast<int32_t>(prev_) + in[i]) / 2);
      out[2 * i + 1] = in[i];
      prev_ = in[i];
    }
  }

 private:
  int16_t prev_ = 0;
};

// Triangle tone from a phase accumulator (no table, no float). Peak = +-32767: apply gain after.
class ToneGen {
 public:
  void set(uint32_t freq_hz, uint32_t rate_hz) {
    step_ = static_cast<uint32_t>((static_cast<uint64_t>(freq_hz) << 32) / rate_hz);
  }
  int16_t next() {
    phase_ += step_;
    int32_t p = static_cast<int32_t>(phase_ >> 16);  // 0..65535
    int32_t tri = (p < 32768) ? p : 65535 - p;       // 0..32767..0
    return static_cast<int16_t>((tri - 16384) * 2);  // -32768..32766
  }

 private:
  uint32_t phase_ = 0, step_ = 0;
};

}  // namespace lr
