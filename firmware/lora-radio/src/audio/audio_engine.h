// Audio engine: microphone -> Codec2 1300 -> packed pairs; packed pairs -> speaker; siren and beeps.
// Runs in its own task on core 0 (Codec2 needs a large stack, see AUDIO_TASK_STACK), commanded from
// the main loop through a queue. The I2S port is opened only while it is in use (INMP441 and the
// MAX98357 sleep when the clocks stop; required for light-sleep).
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <stddef.h>
#include <stdint.h>

#include "../protocol/protocol.h"

namespace lr {

enum SirenKind : uint8_t {
  SIREN_NONE = 0,
  SIREN_SOS,      // alternating 600/900 Hz, 250 ms each
  SIREN_PELIGRO,  // three 800 Hz beeps, repeated
  BEEP_YES,       // short 1000 Hz
  BEEP_NO,        // short 600 Hz
  BEEP_ERROR,     // low double beep
};

enum AudioState : uint8_t { AUDIO_IDLE, AUDIO_PREPARING, AUDIO_RECORDING, AUDIO_PLAYING, AUDIO_ALERT };

struct RecordResult {
  uint16_t pairs;     // 0 = nothing usable
  bool full;          // stopped by the 10 s limit
  bool failed;        // I2S / codec error
};

class AudioEngine {
 public:
  static constexpr size_t MAX_CLIP_BYTES_ = 125 * PAIR_BYTES;  // 10 s = MAX_CLIP_PAIRS in node_fsm.h

  // Allocates Codec2 and starts the task. false = out of memory or Codec2 mode mismatch.
  bool begin();
  void set_volume(uint8_t level);
  uint8_t volume() const { return volume_; }

  bool start_record();          // false if the engine is busy
  void stop_record();           // end of PTT: result via poll_record()
  void cancel_record();         // discard (SOS pressed, alarm received)
  bool poll_record(RecordResult* r);
  const uint8_t* clip_data() const { return rec_buf_; }

  // Copies the clip: the caller may reuse its buffer at once. Fragments listed in
  // `missing_frag_mask` (bit i = fragment i) are played as silence.
  bool play_clip(const uint8_t* data, uint16_t pairs, uint16_t missing_frag_mask);
  void alert(SirenKind kind);   // starts a siren/beep (replaces whatever plays)
  void stop();                  // stops playback or alert at once
  AudioState state() const { return state_; }
  bool idle() const { return state_ == AUDIO_IDLE; }
  uint32_t last_encode_ms() const { return enc_ms_max_; }   // worst Codec2 encode time per 40 ms frame
  uint32_t last_decode_ms() const { return dec_ms_max_; }

 private:
  static void task_entry(void* arg);
  void task();
  void run_record();
  void run_play();
  void run_alert();

  volatile AudioState state_ = AUDIO_IDLE;
  volatile uint8_t volume_ = 3;
  volatile bool stop_flag_ = false;
  volatile bool rec_discard_ = false;
  volatile bool rec_done_ = false;
  RecordResult rec_result_ = {0, false, false};
  uint8_t rec_buf_[MAX_CLIP_BYTES_];
  uint8_t play_buf_[MAX_CLIP_BYTES_];
  uint16_t play_pairs_ = 0;
  uint16_t play_missing_ = 0;
  SirenKind alert_kind_ = SIREN_NONE;
  uint32_t enc_ms_max_ = 0, dec_ms_max_ = 0;
  void* cmd_q_ = nullptr;
  void* c2_ = nullptr;

};

}  // namespace lr
