// LoRa time-on-air (Semtech AN1200.13 / SX1276 DS 4.1.1.6) and the two radio profiles.
// Same formula the scientist used: docs in Ciencia\03-Presupuestos-de-error\LoRa-Radio.md sec. 2.1.
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <math.h>
#include <stdint.h>

#include "protocol.h"

namespace lr {

struct LoraParams {
  uint8_t sf;           // spreading factor 7..12
  uint32_t bw_hz;       // bandwidth [Hz]
  uint8_t cr;           // coding rate denominator offset: 1 = 4/5 ... 4 = 4/8
  uint8_t preamble;     // programmed preamble symbols
};

// Both profiles: BW500 = "modulacion digital" under IFT-008-2015 (no dwell limit), 915 MHz.
constexpr LoraParams PARAMS_ALARM = {12, 500000, 1, 8};
constexpr LoraParams PARAMS_VOICE = {9, 500000, 1, 8};

inline const LoraParams& profile_params(ProfileId p) {
  return p == PROFILE_VOICE_C2_1300 ? PARAMS_VOICE : PARAMS_ALARM;
}

// Time on air [ms] of a packet with `payload_len` bytes: explicit header, CRC on.
inline float toa_ms_f(const LoraParams& p, size_t payload_len) {
  const float t_sym = static_cast<float>(1u << p.sf) * 1000.0f / static_cast<float>(p.bw_hz);
  const int de = (t_sym > 16.0f) ? 1 : 0;  // low-data-rate optimize, mandatory above 16 ms
  const float num = 8.0f * payload_len - 4.0f * p.sf + 28.0f + 16.0f;  // CRC=1, H=0
  const float den = 4.0f * (p.sf - 2 * de);
  float n = ceilf(num / den) * (p.cr + 4);
  if (n < 0) n = 0;
  const float n_payload = 8.0f + n;
  return (p.preamble + 4.25f + n_payload) * t_sym;
}

// Whole milliseconds, rounded up (timeouts must not be short).
inline uint32_t toa_ms(ProfileId profile, size_t payload_len) {
  return static_cast<uint32_t>(ceilf(toa_ms_f(profile_params(profile), payload_len)));
}

}  // namespace lr
