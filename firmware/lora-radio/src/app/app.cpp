// SPDX-License-Identifier: GPL-3.0-or-later
#include "app.h"

#include <Arduino.h>
#include <Preferences.h>
#include <driver/gpio.h>
#include <esp_heap_caps.h>
#include <esp_sleep.h>
#include <esp_system.h>
#include <esp_task_wdt.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>

#include "../audio/audio_engine.h"
#include "../config/config.h"
#include "../config/pins.h"
#include "../drivers/battery_adc.h"
#include "../drivers/buttons.h"
#include "../drivers/display_oled.h"
#include "../drivers/radio_sx1262.h"
#include "../protocol/node_fsm.h"
#include "battery_logic.h"
#include "ui_logic.h"

namespace lr {

namespace {

constexpr uint32_t WDT_TIMEOUT_MS = 10000;     // loop must come around at least this often (sleep is capped at 1 s)
constexpr uint32_t TX_WATCHDOG_MS = 1500;      // longest legal packet is ~313 ms (SF9 255 B)
constexpr uint32_t UI_PERIOD_MS = 5;
constexpr uint8_t VOICE_DST = NODE_BROADCAST;  // clips and messages go to everyone

// ---- state -------------------------------------------------------------------------------------
RadioSx1262 g_radio;
DisplayOled g_oled;
Buttons g_btn;
BatteryAdc g_batadc;
BatteryMonitor g_bat({VBAT_CUTOFF_LOADED_MV, VBAT_CUTOFF_REST_MV, VBAT_WARN_MV, VBAT_NO_BATTERY_MV, VBAT_CUTOFF_HOLD_MS});
UiLogic g_ui(UiConfig{SOS_HOLD_MS, MENU_LONG_MS, MENU_TIMEOUT_MS});
AudioEngine g_audio;
Preferences g_prefs;
NodeFsm* g_fsm = nullptr;
bool g_voice_ok = false;

uint8_t g_node_id = NODE_ID_DEFAULT;
bool g_log = true;
bool g_sleep_enabled = true;
uint32_t g_serial_awake_until = SERIAL_AWAKE_MS;
char g_line[48];
size_t g_line_len = 0;

float g_rssi = 0, g_snr = 0;
bool g_have_link = false;
char g_last[24] = "--";
char g_notice[24] = "";
uint32_t g_notice_until = 0;
uint32_t g_display_until = OLED_TIMEOUT_MS;
uint32_t g_next_draw = 0, g_next_ui = 0, g_next_bat = 0;
uint32_t g_ptt_release_ms = 0, g_clip_rx_start_ms = 0;
bool g_radio_fault = false;
uint32_t g_radio_retry_at = 0;

struct Alarm {
  bool active = false, siren_on = false;
  uint8_t src = 0, type = 0;
  float rssi = 0, snr = 0;
  uint32_t until = 0;
} g_alarm;

struct AutoPrueba {
  uint16_t left = 0;
  uint32_t period = PRUEBA_MIN_PERIOD_MS, next = 0;
} g_auto;

// ---- log -----------------------------------------------------------------------------------------
// LOG,<ms>,<ev>,<self>,<peer>,<type>,<id>,<rssi>,<snr>,<a>,<b>,<c>,<d>   (see docs/protocolo.md sec. 9)
void log_row(const char* ev, uint8_t peer, uint8_t type, uint16_t id, float rssi, float snr, long a = 0, long b = 0,
             long c = 0, long d = 0) {
  if (!g_log) return;
  Serial.printf("LOG,%lu,%s,%u,%u,%u,%u,%.1f,%.2f,%ld,%ld,%ld,%ld\n", static_cast<unsigned long>(millis()), ev, g_node_id,
                peer, type, id, rssi, snr, a, b, c, d);
}

void set_notice(uint32_t now, const char* fmt, ...) {
  va_list ap;
  va_start(ap, fmt);
  vsnprintf(g_notice, sizeof g_notice, fmt, ap);
  va_end(ap);
  g_notice_until = now + 5000;
  g_display_until = now + OLED_TIMEOUT_MS;
}

void wake_display(uint32_t now) { g_display_until = now + OLED_TIMEOUT_MS; }

// ---- radio host adapter ------------------------------------------------------------------------
class FwHost : public Host {
 public:
  void radio_set_profile(ProfileId p) override {
    if (!g_radio.set_profile(p)) g_radio_fault = true;
  }
  bool radio_tx(const uint8_t* buf, size_t len) override { return g_radio.start_tx(buf, len); }
  void notify(const Event& e) override;
} g_host;

const char* clip_tx_text(uint8_t code) {
  switch (code) {
    case CLIPTX_OK: return "CLIP ENTREGADO";
    case CLIPTX_PARTIAL: return "CLIP CON HUECOS";
    case CLIPTX_NO_RESPONSE: return "CLIP SIN RESPUESTA";
    case CLIPTX_INCOMPLETE: return "CLIP INCOMPLETO";
    case CLIPTX_ABORTED_SOS: return "CLIP CANCELADO";
    case CLIPTX_REMOTE_SOS: return "CLIP CANCELADO";
    case CLIPTX_CANCELLED: return "CLIP CANCELADO";
    default: return "ERROR RADIO";
  }
}

void start_alarm(uint32_t now, const Event& e) {
  g_alarm.active = true;
  g_alarm.siren_on = true;
  g_alarm.src = e.src;
  g_alarm.type = e.type;
  g_alarm.rssi = e.rssi;
  g_alarm.snr = e.snr;
  g_alarm.until = now + ALARM_MAX_MS;
  wake_display(now);
  g_audio.alert(e.type == T_SOS ? SIREN_SOS : SIREN_PELIGRO);
}

void FwHost::notify(const Event& e) {
  const uint32_t now = millis();
  switch (e.kind) {
    case EV_MSG_RX:
      g_rssi = e.rssi;
      g_snr = e.snr;
      g_have_link = true;
      snprintf(g_last, sizeof g_last, "n%u %s", e.src, message_text(e.type));
      log_row("RX", e.src, e.type, e.msg_id, e.rssi, e.snr);
      wake_display(now);
      g_audio.cancel_record();  // an alarm outranks a recording in progress
      if (e.type == T_SOS || e.type == T_PELIGRO) {
        start_alarm(now, e);
      } else if (e.type == T_SI || e.type == T_NO) {
        g_audio.alert(e.type == T_SI ? BEEP_YES : BEEP_NO);
      }  // T_PRUEBA: silent by design, only logged and shown
      break;
    case EV_MSG_ACKED:
      g_rssi = e.rssi;
      g_snr = e.snr;
      g_have_link = true;
      set_notice(now, "ACK n%u (%u)", e.src, e.tries);
      log_row("ACK", e.src, e.type, e.msg_id, e.rssi, e.snr, static_cast<long>(e.rssi_remote), static_cast<long>(e.snr_remote * 100), e.tries);
      break;
    case EV_ACK_EXTRA:
      set_notice(now, "ACK x%u", e.count);
      log_row("ACK2", e.src, e.type, e.msg_id, e.rssi, e.snr, e.count);
      break;
    case EV_MSG_FAILED:
      set_notice(now, e.code ? "REEMPLAZADO" : "SIN ACK");
      log_row("FAIL", 0, e.type, e.msg_id, 0, 0, e.code, 0, e.tries);
      break;
    case EV_TX:
      log_row("TX", e.dst, e.type, e.msg_id, 0, 0, e.count);
      break;
    case EV_CLIP_TX_DONE:
      set_notice(now, "%s", clip_tx_text(e.code));
      log_row("CLIPTX", e.src, T_VANN, e.msg_id, e.rssi, e.snr, e.code, e.mask, e.tries, static_cast<long>(now - g_ptt_release_ms));
      break;
    case EV_CLIP_RX_START:
      g_clip_rx_start_ms = now;
      wake_display(now);
      g_audio.cancel_record();
      log_row("CLIPSTART", e.src, T_VANN, e.msg_id, 0, 0, e.count);
      break;
    case EV_CLIP_RX_DONE: {
      const ClipRx& c = g_fsm->clip_rx();
      g_rssi = e.rssi;
      g_snr = e.snr;
      g_have_link = true;
      log_row("CLIPRX", e.src, T_VANN, e.msg_id, e.rssi, e.snr, e.code, e.mask, e.tries, static_cast<long>(now - g_clip_rx_start_ms));
      if (e.code == CLIPRX_DUPLICATE) break;
      snprintf(g_last, sizeof g_last, "n%u CLIP%s", e.src, e.code == CLIPRX_PARTIAL ? " (HUECO)" : "");
      wake_display(now);
      if (g_voice_ok) g_audio.play_clip(c.data(), c.pairs(), e.mask);
      break;
    }
    case EV_CLIP_RX_ABORT:
      log_row("CLIPABORT", e.src, T_VANN, 0, 0, 0, e.code);
      set_notice(now, e.code == CLIPRX_LOCAL_SOS ? "CLIP DESCARTADO" : "CLIP CANCELADO");
      break;
    case EV_RADIO_ERROR:
      log_row("RADIOERR", 0, 0, 0, 0, 0, g_radio.last_error());
      set_notice(now, "ERROR RADIO %d", g_radio.last_error());
      break;
  }
}

// ---- actions ---------------------------------------------------------------------------------------
void send_msg(uint8_t type, uint32_t now) {
  if (type == T_SOS) g_audio.stop();  // SOS: nothing may keep the speaker or mic busy
  NodeFsm::SendStatus st = g_fsm->send_message(type, VOICE_DST, now);
  if (st == NodeFsm::SEND_BUSY) {
    set_notice(now, "OCUPADO");
    g_audio.alert(BEEP_ERROR);
  } else if (st == NodeFsm::SEND_OK) {
    set_notice(now, "ENVIANDO %s", message_text(type));
  }
  wake_display(now);
}

void handle_ui(uint32_t now) {
  UiAction a;
  while (g_ui.next(&a)) {
    switch (a.kind) {
      case UA_ANY_PRESS:
        wake_display(now);
        if (g_alarm.active) {
          if (g_alarm.siren_on) {
            g_alarm.siren_on = false;  // 1st press: silence, notice stays
            g_audio.stop();
          } else {
            g_alarm.active = false;    // 2nd press: back to the normal screen
          }
        }
        break;
      case UA_PTT_START:
        if (g_alarm.active) break;
        if (!g_voice_ok || !g_audio.idle() || g_fsm->role() != NodeFsm::Role::NONE || g_fsm->message_pending()) {
          set_notice(now, "OCUPADO");
          break;
        }
        g_audio.start_record();
        break;
      case UA_PTT_STOP:
        g_audio.stop_record();
        g_ptt_release_ms = now;
        break;
      case UA_SEND_MESSAGE:
        g_audio.cancel_record();
        send_msg(a.arg, now);
        break;
      case UA_VOLUME_NEXT: {
        uint8_t v = (g_audio.volume() + 1) % VOLUME_LEVELS;
        g_audio.set_volume(v);
        g_prefs.putUChar("vol", v);
        set_notice(now, "VOLUMEN %u", v);
        break;
      }
      default:
        break;
    }
  }
}

void handle_record_done(uint32_t now) {
  RecordResult r;
  if (!g_audio.poll_record(&r)) return;
  g_ptt_release_ms = now;
  if (r.failed) {
    set_notice(now, "ERROR MIC");
    log_row("MICERR", 0, 0, 0, 0, 0);
    return;
  }
  if (r.pairs < 3) {  // < 240 ms: an accidental tap
    set_notice(now, "CLIP MUY CORTO");
    return;
  }
  NodeFsm::SendStatus st = g_fsm->send_clip(VOICE_DST, g_audio.clip_data(), r.pairs * PAIR_BYTES, now);
  if (st != NodeFsm::SEND_OK) set_notice(now, "OCUPADO");
  else set_notice(now, "ENVIANDO CLIP");
  log_row("CLIPSEND", VOICE_DST, T_VANN, 0, 0, 0, r.pairs, r.full ? 1 : 0, g_audio.last_encode_ms());
}

// ---- serial console --------------------------------------------------------------------------------
void print_status(uint32_t now) {
  Serial.printf("STATUS fw=%s id=%u core=%s heap=%u maxblk=%u psram=%u bat=%umV(%d%%) c2enc_max=%ums c2dec_max=%ums "
                "rx_ok=%u rx_bad=%u foreign=%u dup=%u tx=%u txfail=%u up=%lus\n",
                FW_VERSION, g_node_id, ESP.getSdkVersion(), ESP.getFreeHeap(),
                static_cast<unsigned>(heap_caps_get_largest_free_block(MALLOC_CAP_8BIT)), ESP.getPsramSize(),
                g_bat.mv(), g_bat.percent(), g_audio.last_encode_ms(), g_audio.last_decode_ms(), g_fsm->stats().rx_ok,
                g_fsm->stats().rx_bad, g_fsm->stats().rx_foreign, g_fsm->stats().rx_dup, g_fsm->stats().tx,
                g_fsm->stats().tx_fail, static_cast<unsigned long>(now / 1000));
}

void serial_command(char* line, uint32_t now) {
  unsigned a = 0, b = 0;
  if (strcmp(line, "help") == 0) {
    Serial.println("Commands: id N | sleep 0|1 | log 0|1 | prueba N [period_ms] | status | help");
  } else if (sscanf(line, "id %u", &a) == 1) {
    if (a >= 1 && a <= 254) {
      g_prefs.putUChar("id", static_cast<uint8_t>(a));
      Serial.printf("OK id=%u stored; reboot to apply\n", a);
    } else {
      Serial.println("ERR id must be 1..254");
    }
  } else if (sscanf(line, "sleep %u", &a) == 1) {
    g_sleep_enabled = a != 0;
    Serial.printf("OK sleep=%u\n", a);
  } else if (sscanf(line, "log %u", &a) == 1) {
    g_log = a != 0;
    Serial.printf("OK log=%u\n", a);
  } else if (sscanf(line, "prueba %u %u", &a, &b) >= 1) {
    g_auto.left = static_cast<uint16_t>(a);
    g_auto.period = (b >= PRUEBA_MIN_PERIOD_MS) ? b : PRUEBA_MIN_PERIOD_MS;
    g_auto.next = now;
    Serial.printf("OK prueba n=%u period=%lums\n", a, static_cast<unsigned long>(g_auto.period));
  } else if (strcmp(line, "status") == 0) {
    print_status(now);
  } else if (line[0]) {
    Serial.println("ERR unknown command (help)");
  }
}

void serial_poll(uint32_t now) {
  while (Serial.available() > 0) {
    int c = Serial.read();
    g_serial_awake_until = now + SERIAL_AWAKE_MS;
    if (c == '\r') continue;
    if (c == '\n') {
      g_line[g_line_len] = 0;
      serial_command(g_line, now);
      g_line_len = 0;
    } else if (g_line_len < sizeof(g_line) - 1) {
      g_line[g_line_len++] = static_cast<char>(c);
    }
  }
}

// ---- display ---------------------------------------------------------------------------------------
const char* state_text() {
  if (g_audio.state() == AUDIO_PREPARING || g_audio.state() == AUDIO_RECORDING) return "GRABANDO";
  if (g_audio.state() == AUDIO_PLAYING) return "REPRODUCIENDO";
  if (g_fsm->role() == NodeFsm::Role::VRX) return "RECIBIENDO";
  if (g_fsm->role() == NodeFsm::Role::VTX || g_fsm->tx_busy() || g_fsm->message_pending()) return "ENVIANDO";
  return "ESCUCHA";
}

void draw(uint32_t now) {
  StatusModel m;
  m.node_id = g_node_id;
  m.bat_percent = g_bat.percent();
  m.bat_mv = g_bat.mv();
  m.bat_none = g_bat.state() == BAT_NONE;
  m.bat_low = g_bat.state() == BAT_LOW;
  m.have_link = g_have_link;
  m.rssi = g_rssi;
  m.snr = g_snr;
  m.state = state_text();
  m.last = g_last;
  m.notice = (now < g_notice_until) ? g_notice : "";
  m.volume = g_audio.volume();
  m.menu_open = g_ui.menu_open();
  m.menu_index = g_ui.menu_index();
  m.sos_progress = g_ui.sos_progress(now);
  m.alarm_active = g_alarm.active;
  m.alarm_flash = g_alarm.siren_on && ((now / 250) & 1);
  m.alarm_title = (g_alarm.type == T_SOS) ? "SOS / AYUDA" : "PELIGRO";
  m.alarm_src = g_alarm.src;
  m.rssi = g_alarm.active ? g_alarm.rssi : g_rssi;
  m.snr = g_alarm.active ? g_alarm.snr : g_snr;
  g_oled.draw(m);
}

// ---- power -----------------------------------------------------------------------------------------
[[noreturn]] void shutdown_low_battery() {
  g_oled.power(true);
  g_oled.message("BATERIA AGOTADA", "CARGAR Y REINICIAR");
  g_audio.stop();
  g_radio.sleep();
  delay(3000);
  g_oled.power(false);
  esp_deep_sleep_start();  // no wake source: only reset/USB brings it back
  for (;;) {}
}

void maybe_sleep(uint32_t now) {
  if (!g_sleep_enabled || static_cast<int32_t>(now - g_serial_awake_until) < 0) return;
  if (!g_fsm->can_sleep() || g_btn.any_raw_pressed() || g_ui.any_pressed() || g_ui.menu_open()) return;
  if (!g_audio.idle() || g_alarm.active || g_radio.tx_active() || g_auto.left > 0) return;
  if (digitalRead(PIN_LORA_DIO1) == HIGH) return;  // a packet is waiting
  uint32_t wait = SLEEP_MAX_MS;
  const uint32_t w = g_fsm->ms_until_work(now);
  if (w < wait) wait = w;
  if (g_oled.is_on()) {
    int32_t until = static_cast<int32_t>(g_display_until - now);
    if (until < static_cast<int32_t>(wait)) wait = until > 0 ? static_cast<uint32_t>(until) : 0;
  }
  if (wait < 5) return;  // not worth the wake-up latency
  Serial.flush();
  esp_sleep_enable_timer_wakeup(static_cast<uint64_t>(wait) * 1000ULL);
  gpio_wakeup_enable(static_cast<gpio_num_t>(PIN_LORA_DIO1), GPIO_INTR_HIGH_LEVEL);
  g_btn.arm_wakeup();
  esp_sleep_enable_gpio_wakeup();
  esp_light_sleep_start();
}

}  // namespace

// ---- setup / loop ----------------------------------------------------------------------------------
void app_setup() {
  // Outputs that must be quiet before anything else (MTDO/GPIO15 is high at reset).
  pinMode(PIN_AMP_SD, OUTPUT);
  digitalWrite(PIN_AMP_SD, LOW);
  pinMode(PIN_LORA_RXEN, OUTPUT);
  digitalWrite(PIN_LORA_RXEN, LOW);
  pinMode(PIN_LORA_TXEN, OUTPUT);
  digitalWrite(PIN_LORA_TXEN, LOW);

  Serial.begin(115200);
  delay(50);
  const uint32_t now = millis();

  if (g_prefs.begin("lora", false)) {
    g_node_id = g_prefs.getUChar("id", NODE_ID_DEFAULT);
    if (g_node_id == 0 || g_node_id == NODE_BROADCAST) g_node_id = NODE_ID_DEFAULT;
  }
  const uint8_t vol = g_prefs.getUChar("vol", VOLUME_DEFAULT);

  esp_task_wdt_config_t wdt = {WDT_TIMEOUT_MS, 0, true};
  if (esp_task_wdt_init(&wdt) != ESP_OK) esp_task_wdt_reconfigure(&wdt);
  esp_task_wdt_add(nullptr);

  const bool have_oled = g_oled.begin();
  char l2[24];
  snprintf(l2, sizeof l2, "ID %u  v%s", g_node_id, FW_VERSION);
  g_oled.message("LoRa-Radio", l2);

  g_btn.begin();
  g_batadc.begin();
  g_bat.update(now, g_batadc.read_mv(), false);

  Serial.printf("\nLoRa-Radio fw %s node %u | oled=%d | arduino-esp32 %s\n", FW_VERSION, g_node_id, have_oled,
                ESP.getSdkVersion());

  while (!g_radio.begin()) {  // not energized yet: safe to wait and retry
    Serial.printf("ERR radio begin failed, RadioLib code %d (-2 = chip not found: check SPI/BUSY/TCXO)\n", g_radio.last_error());
    snprintf(l2, sizeof l2, "CODIGO %d", g_radio.last_error());
    g_oled.message("ERROR RADIO", l2);
    for (int i = 0; i < 50; i++) {
      esp_task_wdt_reset();
      delay(100);
    }
  }

  g_voice_ok = g_audio.begin();
  if (!g_voice_ok) {
    Serial.println("ERR audio/Codec2 init failed: voice disabled, alarms still work");
    g_oled.message("SIN VOZ", "ERROR CODEC2");
    delay(1500);
  } else {
    g_audio.set_volume(vol);
  }

  g_fsm = new NodeFsm(g_host, g_node_id, static_cast<uint16_t>(esp_random() & 0xFFFF));
  g_fsm->begin(now);
  g_display_until = now + OLED_TIMEOUT_MS;
  g_serial_awake_until = now + SERIAL_AWAKE_MS;
  if (g_bat.state() == BAT_CUTOFF) shutdown_low_battery();
  Serial.printf("LOG,%lu,BOOT,%u,0,0,0,0,0,%d,%u,%d,0\n", static_cast<unsigned long>(millis()), g_node_id, g_voice_ok,
                ESP.getFreeHeap(), have_oled);
  print_status(now);
}

void app_loop() {
  esp_task_wdt_reset();
  const uint32_t now = millis();

  serial_poll(now);

  // Radio: events first, then the state machine's timers.
  if (g_radio_fault && static_cast<int32_t>(now - g_radio_retry_at) >= 0) {
    g_radio_retry_at = now + 2000;
    if (g_radio.begin()) g_radio_fault = false;
  }
  uint8_t buf[MAX_PACKET];
  for (int i = 0; i < 4; i++) {
    size_t len = 0;
    float rssi = 0, snr = 0;
    RadioSx1262::PollResult r = g_radio.poll(buf, &len, &rssi, &snr);
    if (r == RadioSx1262::POLL_NONE) break;
    if (r == RadioSx1262::POLL_TX_DONE) g_fsm->on_tx_done(now);
    else if (r == RadioSx1262::POLL_RX) g_fsm->on_rx(buf, len, rssi, snr, now);
  }
  if (g_radio.tx_active() && now - g_radio.tx_started_ms() > TX_WATCHDOG_MS) {
    g_radio.abort_tx();  // PA must never stay keyed
    g_fsm->on_tx_done(now);
    log_row("TXWDT", 0, 0, 0, 0, 0);
  }
  g_fsm->tick(now);

  // Auto-PRUEBA for range tests (serial command "prueba N [period]").
  if (g_auto.left > 0 && static_cast<int32_t>(now - g_auto.next) >= 0 && !g_fsm->message_pending() &&
      g_fsm->role() == NodeFsm::Role::NONE) {
    if (g_fsm->send_message(T_PRUEBA, NODE_BROADCAST, now) == NodeFsm::SEND_OK) {
      g_auto.left--;
      g_auto.next = now + g_auto.period;
      set_notice(now, "PRUEBA %u rest", g_auto.left);
    }
  }

  // UI
  if (static_cast<int32_t>(now - g_next_ui) >= 0) {
    g_next_ui = now + UI_PERIOD_MS;
    g_btn.update(now);
    g_ui.update(now, g_btn.ptt(), g_btn.sos(), g_btn.menu(), g_btn.vol());
    handle_ui(now);
  }
  handle_record_done(now);

  if (g_alarm.active && g_alarm.siren_on && static_cast<int32_t>(now - g_alarm.until) >= 0) g_alarm.siren_on = false;
  if (g_alarm.active && g_audio.idle() && g_alarm.siren_on) g_audio.alert(g_alarm.type == T_SOS ? SIREN_SOS : SIREN_PELIGRO);

  // Battery
  if (static_cast<int32_t>(now - g_next_bat) >= 0) {
    g_next_bat = now + VBAT_PERIOD_MS;
    const bool loaded = g_radio.tx_active() || g_audio.state() == AUDIO_PLAYING || g_audio.state() == AUDIO_ALERT;
    if (g_bat.update(now, g_batadc.read_mv(), loaded) == BAT_CUTOFF) shutdown_low_battery();
  }

  // Screen
  const bool want_on = static_cast<int32_t>(g_display_until - now) > 0 || g_alarm.active || g_ui.any_pressed();
  g_oled.power(want_on);
  if (want_on && static_cast<int32_t>(now - g_next_draw) >= 0) {
    g_next_draw = now + 100;
    draw(now);
  }

  maybe_sleep(millis());
  delay(1);
}

}  // namespace lr
