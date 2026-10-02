// SPDX-License-Identifier: GPL-3.0-or-later
#include "buttons.h"

#include <Arduino.h>
#include <driver/gpio.h>

#include "../config/config.h"
#include "../config/pins.h"

namespace lr {

namespace {
const int kPins[4] = {PIN_BTN_PTT, PIN_BTN_SOS, PIN_BTN_MENU, PIN_BTN_VOL};
}

Buttons::Buttons() : d_{Debouncer(DEBOUNCE_MS), Debouncer(DEBOUNCE_MS), Debouncer(DEBOUNCE_MS), Debouncer(DEBOUNCE_MS)} {}

void Buttons::begin() {
  // 34/35/39 are input-only and have no internal pull-up: the external 10 k does the job.
  pinMode(PIN_BTN_PTT, INPUT);
  pinMode(PIN_BTN_SOS, INPUT);
  pinMode(PIN_BTN_MENU, INPUT_PULLUP);
  pinMode(PIN_BTN_VOL, INPUT);
}

void Buttons::update(uint32_t now) {
  for (int i = 0; i < 4; i++) d_[i].update(now, digitalRead(kPins[i]) == LOW);
}

bool Buttons::any_raw_pressed() const {
  for (int i = 0; i < 4; i++)
    if (digitalRead(kPins[i]) == LOW) return true;
  return false;
}

void Buttons::arm_wakeup() const {
  for (int i = 0; i < 3; i++) gpio_wakeup_enable(static_cast<gpio_num_t>(kPins[i]), GPIO_INTR_LOW_LEVEL);
}

}  // namespace lr
