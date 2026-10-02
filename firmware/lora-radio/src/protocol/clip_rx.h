// Clip reassembly: fragments arrive in any order, duplicates and holes are tracked in a bitmap.
// Static storage only. SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <stdint.h>
#include <string.h>

#include "protocol.h"

namespace lr {

class ClipRx {
 public:
  // 8 x 247 B = 1976 B = 12.1 s of Codec2-1300 audio; the clip limit is 10 s (7 fragments).
  static constexpr uint8_t MAX_RX_FRAGS = 8;
  static constexpr size_t CAPACITY = MAX_RX_FRAGS * FRAG_AUDIO_BYTES;

  enum AddResult : uint8_t { ADD_NEW, ADD_DUP, ADD_REJECT };

  void clear() {
    active_ = false;
    got_ = 0;
    frag_cnt_ = 0;
    pairs_ = 0;
  }

  // Returns false when the announced clip does not fit or is inconsistent.
  bool begin(uint8_t src, uint16_t msg_id, uint8_t frag_cnt, uint16_t pairs) {
    clear();
    if (frag_cnt == 0 || frag_cnt > MAX_RX_FRAGS || pairs == 0) return false;
    if (frag_count_for(pairs) != frag_cnt) return false;
    memset(data_, 0, sizeof(data_));  // holes decode as silence-ish, never as stale audio
    src_ = src;
    msg_id_ = msg_id;
    frag_cnt_ = frag_cnt;
    pairs_ = pairs;
    active_ = true;
    return true;
  }

  AddResult add(uint8_t idx, const uint8_t* audio, uint8_t pairs) {
    if (!active_ || idx >= frag_cnt_ || pairs != frag_pairs(pairs_, idx)) return ADD_REJECT;
    if (got_ & (1u << idx)) return ADD_DUP;
    memcpy(data_ + static_cast<size_t>(idx) * FRAG_AUDIO_BYTES, audio, pairs * PAIR_BYTES);
    got_ |= static_cast<uint16_t>(1u << idx);
    return ADD_NEW;
  }

  uint16_t all_mask() const { return static_cast<uint16_t>((1u << frag_cnt_) - 1u); }
  uint16_t missing_mask() const { return static_cast<uint16_t>(all_mask() & ~got_); }
  bool complete() const { return active_ && missing_mask() == 0; }
  bool has_fragment(uint8_t idx) const { return idx < frag_cnt_ && (got_ & (1u << idx)); }
  uint8_t received() const {
    uint8_t n = 0;
    for (uint8_t i = 0; i < frag_cnt_; i++) n += (got_ >> i) & 1u;
    return n;
  }

  bool active() const { return active_; }
  uint8_t src() const { return src_; }
  uint16_t msg_id() const { return msg_id_; }
  uint8_t frag_cnt() const { return frag_cnt_; }
  uint16_t pairs() const { return pairs_; }
  const uint8_t* data() const { return data_; }

 private:
  uint8_t data_[CAPACITY];
  uint16_t got_ = 0;
  uint16_t msg_id_ = 0;
  uint16_t pairs_ = 0;
  uint8_t src_ = 0;
  uint8_t frag_cnt_ = 0;
  bool active_ = false;
};

}  // namespace lr
