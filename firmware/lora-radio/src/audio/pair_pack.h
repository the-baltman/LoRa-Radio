// Packs two Codec2-1300 frames (52 bit each, MSB-first, zero-padded to 7 B by codec2_encode)
// into 13 bytes: saves 1 B per 80 ms (7 %) [scientist 4.1]. Portable and host-tested.
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <stdint.h>

namespace lr {

constexpr int C2_FRAME_BYTES = 7;   // (52 + 7) / 8, what codec2_encode() writes for mode 1300
constexpr int C2_PAIR_BYTES = 13;   // 104 bit

// a, b: 7 B each (low nibble of byte 6 is padding and is ignored). out: 13 B.
inline void pack_pair(const uint8_t* a, const uint8_t* b, uint8_t* out) {
  for (int i = 0; i < 6; i++) out[i] = a[i];
  out[6] = static_cast<uint8_t>((a[6] & 0xF0) | (b[0] >> 4));
  for (int i = 0; i < 6; i++) out[7 + i] = static_cast<uint8_t>((b[i] << 4) | (b[i + 1] >> 4));
}

// in: 13 B. a, b: 7 B each (padding nibble written as 0, as codec2 expects).
inline void unpack_pair(const uint8_t* in, uint8_t* a, uint8_t* b) {
  for (int i = 0; i < 6; i++) a[i] = in[i];
  a[6] = static_cast<uint8_t>(in[6] & 0xF0);
  b[0] = static_cast<uint8_t>((in[6] << 4) | (in[7] >> 4));
  for (int i = 1; i < 6; i++) b[i] = static_cast<uint8_t>((in[6 + i] << 4) | (in[7 + i] >> 4));
  b[6] = static_cast<uint8_t>(in[12] << 4);
}

}  // namespace lr
