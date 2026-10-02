// SX1262 driver (Waveshare Core1262-HF) on top of RadioLib 7.5.0. Non-blocking: TX and RX are
// completed from poll(), which the main loop calls. One modem profile at a time (alarm / voice).
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <stddef.h>
#include <stdint.h>

#include "../protocol/protocol.h"

namespace lr {

class RadioSx1262 {
 public:
  enum PollResult : uint8_t { POLL_NONE, POLL_TX_DONE, POLL_RX, POLL_RX_BAD };

  // Resets the chip, configures the alarm profile and starts RX. Returns false on any failure;
  // last_error() holds the RadioLib code (see RadioLib docs, e.g. -2 = chip not found).
  bool begin();
  // Re-tunes SF/BW/CR for `p` and restarts RX. Safe to call from RX or standby, not during TX.
  bool set_profile(ProfileId p);
  // Starts a transmission (returns immediately). false = refused.
  bool start_tx(const uint8_t* buf, size_t len);
  // Call often. POLL_RX fills buf/len/rssi/snr; the radio is back in RX when it returns.
  PollResult poll(uint8_t* buf, size_t* len, float* rssi, float* snr);
  // TX watchdog: abandon a transmission that never reported DONE and go back to RX.
  void abort_tx();
  bool tx_active() const { return tx_active_; }
  uint32_t tx_started_ms() const { return tx_started_; }
  int last_error() const { return err_; }
  void sleep();  // lowest-power state, for shutdown only

 private:
  bool tx_active_ = false;
  uint32_t tx_started_ = 0;
  int err_ = 0;
  ProfileId profile_ = PROFILE_ALARM;
};

}  // namespace lr
