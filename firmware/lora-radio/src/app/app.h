// Application glue: wires radio, protocol FSM, audio, OLED, buttons, battery, serial console and
// light-sleep together. SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

namespace lr {

constexpr const char* FW_VERSION = "0.1.0";

void app_setup();
void app_loop();

}  // namespace lr
