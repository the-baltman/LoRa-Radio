// LoRa-Radio firmware entry point. All logic lives in src/ (one concern per folder).
// Build: scripts/build.sh. Board: ESP32 Dev Module (Steren ESP32-WROOM DevKit, USB-C, CP2102).
// SPDX-License-Identifier: GPL-3.0-or-later
#include "src/app/app.h"

void setup() { lr::app_setup(); }
void loop() { lr::app_loop(); }
