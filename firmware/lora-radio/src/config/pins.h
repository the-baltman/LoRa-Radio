// Pin map: copied literally from Electronica\02-Proyectos\LoRa-Radio\LoRa-Radio.md (rev A).
// Target board: Steren ESP32 DevKit = ESP32-WROOM (classic), USB-C, CP2102 USB-serial [Baltasar, 2026-10-02].
// Do not change a pin here: conflicts go to c-morty (requiere-arbitraje).
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

// --- SX1262 (Waveshare Core1262-HF), VSPI ---
constexpr int PIN_LORA_NSS = 5;     // strapping pin, high at reset: OK (10 k pull-up)
constexpr int PIN_LORA_SCK = 18;
constexpr int PIN_LORA_MISO = 19;
constexpr int PIN_LORA_MOSI = 23;
constexpr int PIN_LORA_RESET = 14;  // pulses at boot: the firmware resets the radio after boot
constexpr int PIN_LORA_BUSY = 27;
constexpr int PIN_LORA_DIO1 = 4;    // IRQ and light-sleep wake source (active high)
constexpr int PIN_LORA_TXEN = 2;    // strapping: must be low/floating at reset
constexpr int PIN_LORA_RXEN = 12;   // MTDI: MUST be low at reset (high = flash at 1.8 V, no boot). No pull-up!

// --- I2S (INMP441 mic + MAX98357 amp share BCLK/WS; half-duplex) ---
constexpr int PIN_I2S_BCLK = 26;
constexpr int PIN_I2S_WS = 25;
constexpr int PIN_I2S_DIN = 33;     // mic -> ESP32
constexpr int PIN_I2S_DOUT = 13;    // ESP32 -> amp
constexpr int PIN_AMP_SD = 15;      // MTDO: high at reset for a few ms; firmware drives 0 = amp off

// --- OLED I2C ---
constexpr int PIN_OLED_SDA = 21;
constexpr int PIN_OLED_SCL = 22;

// --- Buttons: external 10 k pull-up + 100 nF, active LOW. 34/35/39 are input-only (no internal pull-up) ---
constexpr int PIN_BTN_PTT = 34;     // wake source
constexpr int PIN_BTN_SOS = 35;     // wake source
constexpr int PIN_BTN_MENU = 32;    // wake source
constexpr int PIN_BTN_VOL = 39;     // polled only: ADC1 glitch erratum on GPIO36/39 [PV]

// --- Battery divider 1 M / 1 M on VSYS, 100 nF ---
constexpr int PIN_VBAT_ADC = 36;    // ADC1_CH0
