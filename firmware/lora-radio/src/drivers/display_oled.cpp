// SPDX-License-Identifier: GPL-3.0-or-later
#include "display_oled.h"

#include <Arduino.h>
#include <U8g2lib.h>
#include <Wire.h>
#include <stdio.h>

#include "../app/ui_logic.h"
#include "../config/config.h"
#include "../config/pins.h"
#include "../protocol/protocol.h"

namespace lr {

namespace {
constexpr uint8_t kOledAddr = 0x3C;  // [PV: I2C scanner on the board]
#ifdef OLED_SH1106
U8G2_SH1106_128X64_NONAME_F_HW_I2C g_u8(U8G2_R0, U8X8_PIN_NONE);
#else
U8G2_SSD1306_128X64_NONAME_F_HW_I2C g_u8(U8G2_R0, U8X8_PIN_NONE);
#endif
}  // namespace

bool DisplayOled::begin() {
  Wire.begin(PIN_OLED_SDA, PIN_OLED_SCL);
  Wire.setClock(400000);
  Wire.beginTransmission(kOledAddr);
  present_ = (Wire.endTransmission() == 0);
  if (!present_) return false;
  g_u8.setI2CAddress(kOledAddr << 1);
  g_u8.begin();
  g_u8.setFont(u8g2_font_6x10_tf);
  on_ = true;
  return true;
}

void DisplayOled::power(bool on) {
  if (!present_ || on == on_) return;
  g_u8.setPowerSave(on ? 0 : 1);
  on_ = on;
}

void DisplayOled::message(const char* l1, const char* l2) {
  if (!present_) return;
  g_u8.clearBuffer();
  g_u8.drawStr(0, 24, l1);
  g_u8.drawStr(0, 40, l2);
  g_u8.sendBuffer();
}

void DisplayOled::draw(const StatusModel& m) {
  if (!present_ || !on_) return;
  char buf[32];
  g_u8.clearBuffer();

  if (m.alarm_active) {
    if (m.alarm_flash) g_u8.drawBox(0, 0, 128, 64);
    g_u8.setDrawColor(m.alarm_flash ? 0 : 1);
    g_u8.setFont(u8g2_font_logisoso16_tr);
    g_u8.drawStr(4, 26, m.alarm_title);
    g_u8.setFont(u8g2_font_6x10_tf);
    snprintf(buf, sizeof buf, "DE NODO %u", m.alarm_src);
    g_u8.drawStr(4, 44, buf);
    snprintf(buf, sizeof buf, "RSSI %.0f SNR %.1f", m.rssi, m.snr);
    g_u8.drawStr(4, 58, buf);
    g_u8.setDrawColor(1);
    g_u8.sendBuffer();
    return;
  }

  // Row 1: node id, battery
  snprintf(buf, sizeof buf, "ID:%u", m.node_id);
  g_u8.drawStr(0, 9, buf);
  if (m.bat_none) snprintf(buf, sizeof buf, "USB");
  else snprintf(buf, sizeof buf, "%s%d%% %u.%02uV", m.bat_low ? "!" : "", m.bat_percent, m.bat_mv / 1000, (m.bat_mv % 1000) / 10);
  g_u8.drawStr(128 - static_cast<int>(g_u8.getStrWidth(buf)), 9, buf);
  g_u8.drawHLine(0, 11, 128);

  if (m.menu_open) {
    g_u8.drawStr(0, 23, "MENU (MANTEN = ENVIAR)");
    // 5 items do not fit at 10 px: show the selected one with its neighbours
    int sel = m.menu_index;
    snprintf(buf, sizeof buf, "  %s", message_text(UiLogic::menu_type((sel + UiLogic::MENU_ITEMS - 1) % UiLogic::MENU_ITEMS)));
    g_u8.drawStr(0, 36, buf);
    snprintf(buf, sizeof buf, "> %s", message_text(UiLogic::menu_type(sel)));
    g_u8.drawBox(0, 39, 128, 11);
    g_u8.setDrawColor(0);
    g_u8.drawStr(0, 48, buf);
    g_u8.setDrawColor(1);
    snprintf(buf, sizeof buf, "  %s", message_text(UiLogic::menu_type((sel + 1) % UiLogic::MENU_ITEMS)));
    g_u8.drawStr(0, 60, buf);
    g_u8.sendBuffer();
    return;
  }

  // Row 2: link quality
  if (m.have_link) snprintf(buf, sizeof buf, "RSSI %.0f SNR %.1f", m.rssi, m.snr);
  else snprintf(buf, sizeof buf, "RSSI -- SNR --");
  g_u8.drawStr(0, 23, buf);
  // Row 3-4: last message / notice
  g_u8.drawStr(0, 35, "ULT:");
  g_u8.drawStr(26, 35, m.last);
  if (m.notice[0]) g_u8.drawStr(0, 47, m.notice);
  // Row 5: state + volume
  g_u8.drawStr(0, 62, m.state);
  snprintf(buf, sizeof buf, "VOL %u", m.volume);
  g_u8.drawStr(128 - static_cast<int>(g_u8.getStrWidth(buf)), 62, buf);
  if (m.sos_progress > 0) g_u8.drawFrame(0, 50, 128, 4), g_u8.drawBox(0, 50, 128 * m.sos_progress / 100, 4);
  g_u8.sendBuffer();
}

}  // namespace lr
