// 128x64 I2C OLED (SSD1306 by default; build with -DOLED_SH1106 for an SH1106 panel) through U8g2.
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <stdint.h>

namespace lr {

struct StatusModel {
  uint8_t node_id = 0;
  int bat_percent = 0;
  uint16_t bat_mv = 0;
  bool bat_none = false;     // no cell on VSYS: running from the DevKit USB
  bool bat_low = false;
  bool have_link = false;    // an RSSI/SNR reading exists
  float rssi = 0, snr = 0;
  const char* state = "";    // ESCUCHA / GRABANDO / ENVIANDO / RECIBIENDO / REPRODUCIENDO
  const char* last = "";     // last message or clip, e.g. "n2 PELIGRO"
  const char* notice = "";   // transient line: ACK, errors
  uint8_t volume = 0;        // 0..5
  bool menu_open = false;
  int menu_index = 0;        // into UiLogic::menu_type()
  int sos_progress = 0;      // 0..100 while SOS is held
  bool alarm_active = false; // siren notice: full-screen
  bool alarm_flash = false;  // toggled by the caller to blink
  const char* alarm_title = "";
  uint8_t alarm_src = 0;
};

class DisplayOled {
 public:
  // false = no ACK at 0x3C: the node keeps working without a screen.
  bool begin();
  bool present() const { return present_; }
  void draw(const StatusModel& m);
  void power(bool on);
  bool is_on() const { return on_; }
  void message(const char* l1, const char* l2 = "");  // boot / fatal screens

 private:
  bool present_ = false;
  bool on_ = true;
};

}  // namespace lr
