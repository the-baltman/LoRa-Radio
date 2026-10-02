// SPDX-License-Identifier: GPL-3.0-or-later
#include "battery_adc.h"

#include <Arduino.h>

#include "../config/config.h"
#include "../config/pins.h"

namespace lr {

void BatteryAdc::begin() {
  analogReadResolution(12);
  analogSetPinAttenuation(PIN_VBAT_ADC, ADC_11db);
}

uint16_t BatteryAdc::read_mv() const {
  uint32_t acc = 0;
  for (int i = 0; i < 16; i++) acc += analogReadMilliVolts(PIN_VBAT_ADC);
  float v = (acc / 16.0f) * VBAT_DIV_RATIO * VBAT_CAL_GAIN + VBAT_CAL_OFFSET_MV;
  if (v < 0) v = 0;
  if (v > 65000) v = 65000;
  return static_cast<uint16_t>(v);
}

}  // namespace lr
