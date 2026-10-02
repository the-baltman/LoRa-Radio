// Battery state from VSYS millivolts: percent (Li-ion rest curve, rough), warning, cut-off with hysteresis
// in time. Portable. SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <stdint.h>

namespace lr {

enum BatState : uint8_t { BAT_OK, BAT_LOW, BAT_CUTOFF, BAT_NONE /* DevKit USB only, no cell on VSYS */ };

struct BatteryParams {
  uint16_t cutoff_loaded_mv, cutoff_rest_mv, warn_mv, none_mv;
  uint32_t hold_ms;
};

class BatteryMonitor {
 public:
  explicit BatteryMonitor(const BatteryParams& p) : p_(p) {}

  // mv: calibrated VSYS. loaded: radio TX or speaker active during the sample.
  BatState update(uint32_t now, uint16_t mv, bool loaded) {
    mv_ = mv;
    if (mv < p_.none_mv) {
      state_ = BAT_NONE;
      below_ = false;
      return state_;
    }
    const uint16_t limit = loaded ? p_.cutoff_loaded_mv : p_.cutoff_rest_mv;
    if (mv <= limit) {
      if (!below_) {
        below_ = true;
        below_since_ = now;
      }
      if (static_cast<uint32_t>(now - below_since_) >= p_.hold_ms) state_ = BAT_CUTOFF;
      else if (state_ != BAT_CUTOFF) state_ = BAT_LOW;
    } else {
      below_ = false;
      if (state_ != BAT_CUTOFF) state_ = (mv <= p_.warn_mv) ? BAT_LOW : BAT_OK;
    }
    return state_;
  }

  BatState state() const { return state_; }
  uint16_t mv() const { return mv_; }
  int percent() const { return percent_from_mv(mv_); }

  // Rough 18650 rest curve [EST]; under load the cell sags and this reads low (conservative).
  static int percent_from_mv(uint16_t mv) {
    static const uint16_t v[] = {3300, 3400, 3500, 3600, 3700, 3800, 3900, 4000, 4100, 4200};
    static const uint8_t p[] = {0, 3, 8, 15, 30, 45, 62, 78, 90, 100};
    if (mv <= v[0]) return 0;
    if (mv >= v[9]) return 100;
    for (int i = 1; i < 10; i++)
      if (mv <= v[i]) return p[i - 1] + (p[i] - p[i - 1]) * (mv - v[i - 1]) / (v[i] - v[i - 1]);
    return 100;
  }

 private:
  BatteryParams p_;
  BatState state_ = BAT_OK;
  uint16_t mv_ = 4000;
  bool below_ = false;
  uint32_t below_since_ = 0;
};

}  // namespace lr
