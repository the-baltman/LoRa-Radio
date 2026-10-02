// Four active-low buttons with software debounce and light-sleep wake-up configuration.
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <stdint.h>

#include "../app/debounce.h"

namespace lr {

class Buttons {
 public:
  Buttons();
  void begin();
  // Sample the pins (call every ~5 ms while anything is happening).
  void update(uint32_t now);
  bool ptt() const { return d_[0].state(); }
  bool sos() const { return d_[1].state(); }
  bool menu() const { return d_[2].state(); }
  bool vol() const { return d_[3].state(); }
  // True if any pin reads low right now (raw): sleeping now would wake immediately.
  bool any_raw_pressed() const;
  // Arms PTT/SOS/MENU as low-level wake sources for the next esp_light_sleep_start().
  // VOL (GPIO39) is polled only, see pins.h.
  void arm_wakeup() const;

 private:
  Debouncer d_[4];
};

}  // namespace lr
