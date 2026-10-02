// Tunable constants. Every value says its unit and where it comes from.
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <stdint.h>

// ---- Node identity ---------------------------------------------------------------------------
constexpr uint8_t NODE_ID_DEFAULT = 1;      // 1..254; override at runtime with the serial command "id N" (stored in NVS)

// ---- Radio -----------------------------------------------------------------------------------
constexpr float RF_FREQ_MHZ = 915.0f;       // ISM 902-928 MHz, Mexico. BW500 = digital modulation, IFT-008-2015 4.3 [NORMA]
constexpr int8_t RF_TX_POWER_DBM = 22;      // SX1262 max = hard ceiling of the serial command `pot`; used only after `rfok`. PSD ~ -0.2 dBm/3 kHz at BW500 (limit 8) [scientist 1]; EIRP < 36 dBm
constexpr float RF_OCP_MA = 140.0f;         // SX1262 DS: over-current limit for +22 dBm
constexpr float RF_TCXO_V = 1.8f;           // DEFAULT of the TCXO voltage [PV]; runtime: serial `tcxo <V>` (NVS)
constexpr int8_t RF_UNVERIFIED_POWER_DBM = 0;  // TX power until `rfok` is stored (P-RF-1 not yet passed)
// RXEN/TXEN polarity [PV P-RF-1]: runtime, serial `rfsw 0|1` (NVS). The Waveshare wiki contradicts itself.
constexpr uint16_t RF_PREAMBLE_SYMBOLS = 8;
constexpr float RF_SPI_HZ = 8e6f;           // electronics: <= 8 MHz

// ---- Audio -----------------------------------------------------------------------------------
constexpr uint32_t I2S_RATE_HZ = 16000;     // INMP441 needs SCK >= 0.5 MHz: 16 kHz x 64 = 1.024 MHz; decimated to 8 kHz for Codec2
constexpr int MIC_SHIFT = 14;               // 32-bit slot (24-bit data) -> 16 bit with x4 gain; INMP441 is -26 dBFS @ 94 dB SPL [PV tune on board]
constexpr uint32_t MIC_WARMUP_MS = 260;     // INMP441 wake-up 2^18 SCK cycles ~ 256 ms at 1.024 MHz [DS, PV measure]
constexpr uint16_t MAX_CLIP_SECONDS = 8;    // arbitration 2026-10-02 (SOS < 3 s during a clip); = MAX_CLIP_PAIRS x 80 ms in node_fsm.h
constexpr uint8_t VOLUME_LEVELS = 6;        // 0..5
constexpr uint8_t VOLUME_DEFAULT = 3;
// Output ceiling: 1.84 Vrms differential on the 8 ohm / 0.5 W speaker [electronics]. MAX98357 full scale
// is ~VSYS (<= 4.2 V peak) so 0.5 FS = 2.1 Vpk = 1.48 Vrms for a sine. [PV P-AUD-2: scope SPK_P-SPK_N at max volume]
constexpr int16_t AUDIO_CEILING_Q15 = 16384;      // 0.5 of full scale: DEFAULT and hard cap of the serial command `techo`
constexpr int16_t SIREN_MIN_LEVEL = 3;            // a muted radio must still sound the SOS siren

// ---- UI --------------------------------------------------------------------------------------
constexpr uint32_t DEBOUNCE_MS = 20;
constexpr uint32_t SOS_HOLD_MS = 1500;      // electronics proposal; prevents accidental SOS
constexpr uint32_t MENU_LONG_MS = 1000;     // hold MENU to send the highlighted message
constexpr uint32_t MENU_TIMEOUT_MS = 8000;
constexpr uint32_t OLED_TIMEOUT_MS = 15000; // screen off after this much inactivity
constexpr uint32_t ALARM_MAX_MS = 60000;    // siren stops by itself
constexpr uint32_t PRUEBA_MIN_PERIOD_MS = 3000;  // auto-PRUEBA spacing (cycle ~0.8 s + margin)

// ---- Battery ---------------------------------------------------------------------------------
constexpr float VBAT_DIV_RATIO = 2.0f;      // 1 M / 1 M
constexpr float VBAT_CAL_GAIN = 1.0f;       // identity here; the two-point calibration is runtime (serial `cal`, NVS)
constexpr float VBAT_CAL_OFFSET_MV = 0.0f;
constexpr uint16_t VBAT_CUTOFF_LOADED_MV = 3300;   // electronics: cut at 3.30 V under load
constexpr uint16_t VBAT_CUTOFF_REST_MV = 3400;     // ... 3.40 V at rest
constexpr uint16_t VBAT_WARN_MV = 3500;
constexpr uint16_t VBAT_NO_BATTERY_MV = 2000;      // VSYS below this = running from the DevKit USB only
constexpr uint32_t VBAT_PERIOD_MS = 10000;
constexpr uint32_t VBAT_CUTOFF_HOLD_MS = 30000;    // must stay below the limit this long

// ---- Power -----------------------------------------------------------------------------------
constexpr uint32_t SERIAL_AWAKE_MS = 30000; // no light-sleep for this long after boot or any serial byte (UART is dead in sleep)
constexpr uint32_t SLEEP_MAX_MS = 1000;     // timer wake cap
