// VSYS measurement through the 1 M / 1 M divider on GPIO36 (ADC1_CH0). SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <stdint.h>

namespace lr {

class BatteryAdc {
 public:
  void begin();
  // Calibrated VSYS in mV (average of 16 samples; the 500 k source impedance is covered by the
  // 100 nF on the node, so we take several samples instead of trusting one).
  uint16_t read_mv() const;
};

}  // namespace lr
