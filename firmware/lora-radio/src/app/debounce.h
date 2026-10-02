// Time-based debouncer: the output changes only after the raw level has been stable for `stable_ms`.
// Portable. SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <stdint.h>

namespace lr {

class Debouncer {
 public:
  explicit Debouncer(uint32_t stable_ms = 20) : stable_ms_(stable_ms) {}
  // `raw`: true = pressed. Returns the debounced state.
  bool update(uint32_t now, bool raw) {
    if (raw != last_raw_) {
      last_raw_ = raw;
      since_ = now;
    } else if (raw != out_ && static_cast<uint32_t>(now - since_) >= stable_ms_) {
      out_ = raw;
    }
    return out_;
  }
  bool state() const { return out_; }

 private:
  uint32_t stable_ms_;
  uint32_t since_ = 0;
  bool last_raw_ = false;
  bool out_ = false;
};

}  // namespace lr
