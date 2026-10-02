// SPDX-License-Identifier: GPL-3.0-or-later
// Codec2: esp32_codec2 1.0.7 (GPL-3.0), mode 1300 (52 bit / 40 ms frame).
#include "audio_engine.h"

#include <Arduino.h>
#include <ESP_I2S.h>
#include <codec2.h>
#include <freertos/FreeRTOS.h>
#include <freertos/queue.h>
#include <freertos/task.h>

#include "../config/config.h"
#include "../config/pins.h"
#include "../protocol/node_fsm.h"
#include "dsp.h"
#include "pair_pack.h"

namespace lr {

namespace {

constexpr uint32_t AUDIO_TASK_STACK = 32768;  // bytes: the working Codec2 reference uses the same [scientist 4.1]
constexpr int kFrame8k = 320;                 // 40 ms at 8 kHz
constexpr int kFrame16k = 640;                // 40 ms at I2S_RATE_HZ
constexpr uint8_t CMD_REC = 1, CMD_PLAY = 2, CMD_ALERT = 3;

I2SClass g_i2s;

bool open_i2s() {
  g_i2s.setPins(PIN_I2S_BCLK, PIN_I2S_WS, PIN_I2S_DOUT, PIN_I2S_DIN);
  // Stereo slots: the INMP441 (L/R to GND) drives the left slot; the amp plays both, so it does not
  // matter which channel its SD pin level selects.
  return g_i2s.begin(I2S_MODE_STD, I2S_RATE_HZ, I2S_DATA_BIT_WIDTH_32BIT, I2S_SLOT_MODE_STEREO);
}

void amp(bool on) { digitalWrite(PIN_AMP_SD, on ? HIGH : LOW); }

// Write `n` mono 16-bit samples as stereo 32-bit slots. Blocks at I2S pace.
bool write_mono16(const int16_t* s, int n, int16_t gain) {
  int32_t out[kFrame16k * 2];
  for (int i = 0; i < n; i++) {
    int32_t v = static_cast<int32_t>(apply_gain(s[i], gain)) << 16;
    out[2 * i] = v;
    out[2 * i + 1] = v;
  }
  size_t bytes = static_cast<size_t>(n) * 8;
  return g_i2s.write(reinterpret_cast<const uint8_t*>(out), bytes) == bytes;
}

}  // namespace

bool AudioEngine::begin() {
  pinMode(PIN_AMP_SD, OUTPUT);
  amp(false);  // MTDO is high at reset: shut the amp up as early as possible
  volume_ = VOLUME_DEFAULT;
  CODEC2* c2 = codec2_create(CODEC2_MODE_1300);
  if (c2 == nullptr) return false;
  if (codec2_samples_per_frame(c2) != kFrame8k || codec2_bits_per_frame(c2) != 52) {
    codec2_destroy(c2);
    return false;
  }
  c2_ = c2;
  QueueHandle_t q = xQueueCreate(4, sizeof(uint8_t));
  if (q == nullptr) return false;
  cmd_q_ = q;
  return xTaskCreatePinnedToCore(task_entry, "audio", AUDIO_TASK_STACK, this, 2, nullptr, 0) == pdPASS;
}

void AudioEngine::set_volume(uint8_t level) { volume_ = level >= VOLUME_LEVELS ? VOLUME_LEVELS - 1 : level; }

bool AudioEngine::start_record() {
  if (state_ != AUDIO_IDLE) return false;
  rec_done_ = false;
  rec_discard_ = false;
  stop_flag_ = false;
  state_ = AUDIO_PREPARING;
  uint8_t c = CMD_REC;
  if (xQueueSend(static_cast<QueueHandle_t>(cmd_q_), &c, 0) != pdTRUE) {
    state_ = AUDIO_IDLE;
    return false;
  }
  return true;
}

void AudioEngine::stop_record() {
  if (state_ == AUDIO_PREPARING || state_ == AUDIO_RECORDING) stop_flag_ = true;
}

void AudioEngine::cancel_record() {
  if (state_ == AUDIO_PREPARING || state_ == AUDIO_RECORDING) {
    rec_discard_ = true;
    stop_flag_ = true;
  }
}

bool AudioEngine::poll_record(RecordResult* r) {
  if (!rec_done_) return false;
  *r = rec_result_;
  rec_done_ = false;
  return true;
}

bool AudioEngine::play_clip(const uint8_t* data, uint16_t pairs, uint16_t missing_frag_mask) {
  if (state_ != AUDIO_IDLE || pairs == 0 || pairs > MAX_CLIP_PAIRS) return false;
  memcpy(play_buf_, data, static_cast<size_t>(pairs) * PAIR_BYTES);
  play_pairs_ = pairs;
  play_missing_ = missing_frag_mask;
  stop_flag_ = false;
  state_ = AUDIO_PLAYING;
  uint8_t c = CMD_PLAY;
  if (xQueueSend(static_cast<QueueHandle_t>(cmd_q_), &c, 0) != pdTRUE) {
    state_ = AUDIO_IDLE;
    return false;
  }
  return true;
}

void AudioEngine::alert(SirenKind kind) {
  stop();
  // Wait (bounded) for the task to release the I2S port: an alert must not be lost to a race.
  for (int i = 0; i < 50 && state_ != AUDIO_IDLE; i++) vTaskDelay(pdMS_TO_TICKS(5));
  if (state_ != AUDIO_IDLE) return;
  alert_kind_ = kind;
  stop_flag_ = false;
  state_ = AUDIO_ALERT;
  uint8_t c = CMD_ALERT;
  if (xQueueSend(static_cast<QueueHandle_t>(cmd_q_), &c, 0) != pdTRUE) state_ = AUDIO_IDLE;
}

void AudioEngine::stop() {
  if (state_ == AUDIO_PLAYING || state_ == AUDIO_ALERT) stop_flag_ = true;
  else if (state_ == AUDIO_PREPARING || state_ == AUDIO_RECORDING) cancel_record();
}

void AudioEngine::task_entry(void* arg) { static_cast<AudioEngine*>(arg)->task(); }

void AudioEngine::task() {
  for (;;) {
    uint8_t cmd = 0;
    if (xQueueReceive(static_cast<QueueHandle_t>(cmd_q_), &cmd, portMAX_DELAY) != pdTRUE) continue;
    switch (cmd) {
      case CMD_REC: run_record(); break;
      case CMD_PLAY: run_play(); break;
      case CMD_ALERT: run_alert(); break;
      default: break;
    }
    state_ = AUDIO_IDLE;
  }
}

void AudioEngine::run_record() {
  CODEC2* c2 = static_cast<CODEC2*>(c2_);
  RecordResult res = {0, false, false};
  amp(false);
  if (!open_i2s()) {
    res.failed = true;
    rec_result_ = res;
    rec_done_ = true;
    return;
  }
  static int32_t raw[kFrame16k * 2];
  int16_t pcm16[kFrame16k], pcm8[kFrame8k];
  const size_t raw_bytes = sizeof(raw);

  // The INMP441 needs ~2^18 SCK cycles after the clock starts: discard that audio.
  const uint32_t t0 = millis();
  while (millis() - t0 < MIC_WARMUP_MS && !stop_flag_) {
    if (g_i2s.readBytes(reinterpret_cast<char*>(raw), raw_bytes) != raw_bytes) {
      res.failed = true;
      break;
    }
  }
  Decimator2 dec;
  uint8_t frames[2][C2_FRAME_BYTES];
  int nf = 0;
  uint16_t pairs = 0;
  if (!res.failed && !stop_flag_) state_ = AUDIO_RECORDING;
  while (!res.failed && !stop_flag_) {
    if (g_i2s.readBytes(reinterpret_cast<char*>(raw), raw_bytes) != raw_bytes) {
      res.failed = true;
      break;
    }
    for (int i = 0; i < kFrame16k; i++) pcm16[i] = sat16(raw[2 * i] >> MIC_SHIFT);
    dec.process(pcm16, pcm8, kFrame8k);
    const uint32_t te = micros();
    codec2_encode(c2, frames[nf], pcm8);
    const uint32_t dt = (micros() - te + 500) / 1000;
    if (dt > enc_ms_max_) enc_ms_max_ = dt;
    if (++nf == 2) {
      pack_pair(frames[0], frames[1], rec_buf_ + static_cast<size_t>(pairs) * PAIR_BYTES);
      pairs++;
      nf = 0;
      if (pairs >= MAX_CLIP_PAIRS) {
        res.full = true;
        break;
      }
    }
  }
  if (nf == 1 && !res.failed && pairs < MAX_CLIP_PAIRS) {  // odd frame count: pad with silence
    memset(pcm8, 0, sizeof(pcm8));
    codec2_encode(c2, frames[1], pcm8);
    pack_pair(frames[0], frames[1], rec_buf_ + static_cast<size_t>(pairs) * PAIR_BYTES);
    pairs++;
  }
  g_i2s.end();
  res.pairs = (rec_discard_ || res.failed) ? 0 : pairs;
  rec_result_ = res;
  rec_done_ = !rec_discard_ || res.failed;
}

void AudioEngine::run_play() {
  CODEC2* c2 = static_cast<CODEC2*>(c2_);
  amp(true);
  if (!open_i2s()) {
    amp(false);
    return;
  }
  const int16_t gain = volume_gain_q15(volume_, VOLUME_LEVELS, AUDIO_CEILING_Q15);
  Interpolator2 up;
  int16_t pcm8[kFrame8k], pcm16[kFrame16k];
  uint8_t fr[2][C2_FRAME_BYTES];
  for (uint16_t p = 0; p < play_pairs_ && !stop_flag_; p++) {
    const bool hole = (play_missing_ >> (p / FRAG_PAIRS)) & 1u;
    if (!hole) unpack_pair(play_buf_ + static_cast<size_t>(p) * PAIR_BYTES, fr[0], fr[1]);
    for (int k = 0; k < 2 && !stop_flag_; k++) {
      if (hole) {
        memset(pcm16, 0, sizeof(pcm16));
      } else {
        const uint32_t td = micros();
        codec2_decode(c2, pcm8, fr[k]);
        const uint32_t dt = (micros() - td + 500) / 1000;
        if (dt > dec_ms_max_) dec_ms_max_ = dt;
        up.process(pcm8, pcm16, kFrame8k);
      }
      if (!write_mono16(pcm16, kFrame16k, gain)) {
        stop_flag_ = true;
        break;
      }
    }
  }
  memset(pcm16, 0, sizeof(pcm16));
  write_mono16(pcm16, kFrame16k, 0);  // flush the DMA with silence before cutting the clock
  write_mono16(pcm16, kFrame16k, 0);
  g_i2s.end();
  amp(false);
}

void AudioEngine::run_alert() {
  amp(true);
  if (!open_i2s()) {
    amp(false);
    return;
  }
  const SirenKind kind = alert_kind_;
  const bool loud = (kind == SIREN_SOS || kind == SIREN_PELIGRO);
  int lvl = volume_;
  if (loud && lvl < SIREN_MIN_LEVEL) lvl = SIREN_MIN_LEVEL;
  const int16_t gain = volume_gain_q15(lvl, VOLUME_LEVELS, AUDIO_CEILING_Q15);
  ToneGen tone;
  int16_t buf[kFrame16k];
  uint32_t frame = 0;  // 40 ms each
  const uint32_t max_frames = (kind == BEEP_YES || kind == BEEP_NO) ? 4 : (kind == BEEP_ERROR ? 8 : ALARM_MAX_MS / 40);
  while (!stop_flag_ && frame < max_frames) {
    const uint32_t t_ms = frame * 40;
    uint32_t f = 0;  // 0 = silence
    switch (kind) {
      case SIREN_SOS: f = ((t_ms / 250) & 1) ? 900 : 600; break;
      case SIREN_PELIGRO: {
        uint32_t ph = t_ms % 1200;
        f = (ph < 600 && ((ph / 120) % 2) == 0) ? 800 : 0;  // 3 beeps of 120 ms, then 600 ms gap
        break;
      }
      case BEEP_YES: f = 1000; break;
      case BEEP_NO: f = 600; break;
      case BEEP_ERROR: f = ((t_ms / 80) % 2 == 0) ? 300 : 0; break;
      default: break;
    }
    if (f) tone.set(f, I2S_RATE_HZ);
    for (int i = 0; i < kFrame16k; i++) buf[i] = f ? tone.next() : 0;
    if (!write_mono16(buf, kFrame16k, gain)) break;
    frame++;
  }
  memset(buf, 0, sizeof(buf));
  write_mono16(buf, kFrame16k, 0);
  write_mono16(buf, kFrame16k, 0);
  g_i2s.end();
  amp(false);
}

}  // namespace lr
