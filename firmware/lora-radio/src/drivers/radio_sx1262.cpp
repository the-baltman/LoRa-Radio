// SPDX-License-Identifier: GPL-3.0-or-later
#include "radio_sx1262.h"

#include <Arduino.h>
#include <RadioLib.h>
#include <SPI.h>

#include "../config/config.h"
#include "../config/pins.h"
#include "../protocol/airtime.h"

namespace lr {

namespace {

SPIClass g_spi(VSPI);
Module g_mod(PIN_LORA_NSS, PIN_LORA_DIO1, PIN_LORA_RESET, PIN_LORA_BUSY, g_spi, SPISettings(static_cast<uint32_t>(RF_SPI_HZ), MSBFIRST, SPI_MODE0));
SX1262 g_radio(&g_mod);
volatile bool g_irq = false;

void IRAM_ATTR on_dio1() { g_irq = true; }

// RXEN/TXEN levels per mode. Polarity is [PV P-RF-1]: flip RF_SWITCH_INVERTED in config.h if the
// test shows the inverted level is >= 15 dB stronger.
const uint32_t kPins[Module::RFSWITCH_MAX_PINS] = {PIN_LORA_RXEN, PIN_LORA_TXEN, RADIOLIB_NC, RADIOLIB_NC, RADIOLIB_NC};
const Module::RfSwitchMode_t kTable[] = {
    {Module::MODE_IDLE, {LOW, LOW}},
    {Module::MODE_RX, {RF_SWITCH_INVERTED ? LOW : HIGH, RF_SWITCH_INVERTED ? HIGH : LOW}},
    {Module::MODE_TX, {RF_SWITCH_INVERTED ? HIGH : LOW, RF_SWITCH_INVERTED ? LOW : HIGH}},
    END_OF_MODE_TABLE,
};

}  // namespace

bool RadioSx1262::begin() {
  g_spi.begin(PIN_LORA_SCK, PIN_LORA_MISO, PIN_LORA_MOSI, PIN_LORA_NSS);
  g_radio.setRfSwitchTable(kPins, kTable);
  const LoraParams& a = PARAMS_ALARM;
  err_ = g_radio.begin(RF_FREQ_MHZ, a.bw_hz / 1000.0f, a.sf, 4 + a.cr, SYNC_WORD, RF_TX_POWER_DBM,
                       RF_PREAMBLE_SYMBOLS, RF_TCXO_V, /*useRegulatorLDO=*/false);
  if (err_ != RADIOLIB_ERR_NONE) return false;
  err_ = g_radio.setCurrentLimit(RF_OCP_MA);
  if (err_ != RADIOLIB_ERR_NONE) return false;
  err_ = g_radio.setCRC(2);
  if (err_ != RADIOLIB_ERR_NONE) return false;
  g_radio.setDio1Action(on_dio1);
  profile_ = PROFILE_ALARM;
  tx_active_ = false;
  err_ = g_radio.startReceive();
  return err_ == RADIOLIB_ERR_NONE;
}

bool RadioSx1262::set_profile(ProfileId p) {
  const LoraParams& lp = profile_params(p);
  g_radio.standby();
  // BW first: LDRO is recomputed by RadioLib from SF and BW on every change.
  err_ = g_radio.setBandwidth(lp.bw_hz / 1000.0f);
  if (err_ == RADIOLIB_ERR_NONE) err_ = g_radio.setSpreadingFactor(lp.sf);
  if (err_ == RADIOLIB_ERR_NONE) err_ = g_radio.setCodingRate(4 + lp.cr);
  if (err_ != RADIOLIB_ERR_NONE) return false;
  profile_ = p;
  g_irq = false;
  err_ = g_radio.startReceive();
  return err_ == RADIOLIB_ERR_NONE;
}

bool RadioSx1262::start_tx(const uint8_t* buf, size_t len) {
  if (tx_active_ || len == 0 || len > MAX_PACKET) return false;
  g_irq = false;
  err_ = g_radio.startTransmit(buf, len);
  if (err_ != RADIOLIB_ERR_NONE) {
    g_radio.startReceive();
    return false;
  }
  tx_active_ = true;
  tx_started_ = millis();
  return true;
}

void RadioSx1262::abort_tx() {
  g_radio.standby();
  tx_active_ = false;
  g_irq = false;
  g_radio.startReceive();
}

RadioSx1262::PollResult RadioSx1262::poll(uint8_t* buf, size_t* len, float* rssi, float* snr) {
  // DIO1 is level-triggered inside the chip: reading the pin covers an edge missed during light-sleep.
  if (!g_irq && digitalRead(PIN_LORA_DIO1) == LOW) return POLL_NONE;
  g_irq = false;
  const uint32_t flags = g_radio.getIrqFlags();

  if (tx_active_) {
    if (!(flags & RADIOLIB_SX126X_IRQ_TX_DONE) && !(flags & RADIOLIB_SX126X_IRQ_TIMEOUT)) return POLL_NONE;
    g_radio.finishTransmit();  // clears IRQs, standby
    tx_active_ = false;
    g_radio.startReceive();    // radio contract: back in RX BEFORE the FSM hears about it
    return POLL_TX_DONE;
  }

  if (!(flags & (RADIOLIB_SX126X_IRQ_RX_DONE | RADIOLIB_SX126X_IRQ_CRC_ERR | RADIOLIB_SX126X_IRQ_HEADER_ERR))) {
    g_radio.clearIrqFlags(flags);
    return POLL_NONE;
  }
  size_t n = g_radio.getPacketLength();
  if (n > MAX_PACKET) n = MAX_PACKET;
  int st = g_radio.readData(buf, n);
  *rssi = g_radio.getRSSI();
  *snr = g_radio.getSNR();
  g_radio.startReceive();
  if (st != RADIOLIB_ERR_NONE) return POLL_RX_BAD;
  *len = n;
  return POLL_RX;
}

void RadioSx1262::sleep() { g_radio.sleep(); }

}  // namespace lr
