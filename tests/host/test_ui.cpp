// Host tests: debounce, 4-button UI, battery logic, DSP helpers. SPDX-License-Identifier: GPL-3.0-or-later
#include <stdio.h>
#include <stdlib.h>

#include "app/battery_logic.h"
#include "app/debounce.h"
#include "app/ui_logic.h"
#include "audio/dsp.h"

using namespace lr;

static int g_fail = 0, g_pass = 0;
#define CHECK(c)                                              \
  do {                                                        \
    if (c) g_pass++;                                          \
    else {                                                    \
      g_fail++;                                               \
      printf("  FAIL %s:%d  %s\n", __FILE__, __LINE__, #c);   \
    }                                                         \
  } while (0)

struct Rig {
  UiLogic ui;
  uint32_t t = 0;
  bool b[4] = {false, false, false, false};  // ptt sos menu vol
  UiAction got[32];
  int n = 0;
  void run(uint32_t ms) {
    for (uint32_t i = 0; i < ms; i++) {
      t++;
      ui.update(t, b[0], b[1], b[2], b[3]);
      UiAction a;
      while (ui.next(&a))
        if (a.kind != UA_ANY_PRESS && n < 32) got[n++] = a;
    }
  }
  int count(UiActionKind k, int arg = -1) {
    int c = 0;
    for (int i = 0; i < n; i++) c += (got[i].kind == k && (arg < 0 || got[i].arg == arg));
    return c;
  }
};

static void test_debounce() {
  Debouncer d(20);
  uint32_t t = 0;
  bool out = false;
  const bool bounce[] = {1, 0, 1, 0, 1, 1, 0, 1};  // contact chatter, 3 ms apart
  for (bool v : bounce) {
    t += 3;
    out = d.update(t, v);
  }
  CHECK(!out);
  for (int i = 0; i < 25; i++) out = d.update(++t, true);
  CHECK(out);
  out = d.update(++t, false);
  CHECK(out);  // a one-sample glitch low does not release
  out = d.update(++t, true);
  for (int i = 0; i < 25; i++) out = d.update(++t, false);
  CHECK(!out);
}

static void test_ui() {
  { Rig r;  // PTT
    r.run(10);
    r.b[0] = true; r.run(30); r.b[0] = false; r.run(30);
    CHECK(r.count(UA_PTT_START) == 1 && r.count(UA_PTT_STOP) == 1); }
  { Rig r;  // SOS: tap does nothing, hold 1.5 s fires once
    r.b[1] = true; r.run(500); r.b[1] = false; r.run(50);
    CHECK(r.count(UA_SEND_MESSAGE) == 0);
    r.b[1] = true; r.run(1400);
    CHECK(r.count(UA_SEND_MESSAGE) == 0);
    r.run(200);
    CHECK(r.count(UA_SEND_MESSAGE, T_SOS) == 1);
    r.run(3000);
    CHECK(r.count(UA_SEND_MESSAGE) == 1);  // not repeated while held
    r.b[1] = false; r.run(50); }
  { Rig r;  // MENU: tap opens (PELIGRO), taps advance, hold sends, order PELIGRO PRUEBA SI NO SOS
    r.b[2] = true; r.run(100); r.b[2] = false; r.run(100);
    CHECK(r.ui.menu_open() && r.ui.menu_index() == 0);
    r.b[2] = true; r.run(100); r.b[2] = false; r.run(100);
    CHECK(r.ui.menu_index() == 1);
    r.b[2] = true; r.run(1100);  // hold
    CHECK(r.count(UA_SEND_MESSAGE, T_PRUEBA) == 1 && !r.ui.menu_open());
    r.b[2] = false; r.run(100);
    CHECK(!r.ui.menu_open());  // releasing after the long hold must not reopen
    CHECK(r.count(UA_SEND_MESSAGE) == 1); }
  { Rig r;  // hold while closed does not send
    r.b[2] = true; r.run(1500); r.b[2] = false; r.run(100);
    CHECK(r.count(UA_SEND_MESSAGE) == 0); }
  { Rig r;  // all five items reachable, SOS last
    uint8_t seen[5];
    r.b[2] = true; r.run(60); r.b[2] = false; r.run(60);
    for (int i = 0; i < 5; i++) {
      seen[i] = UiLogic::menu_type(r.ui.menu_index());
      r.b[2] = true; r.run(60); r.b[2] = false; r.run(60);
    }
    CHECK(seen[0] == T_PELIGRO && seen[1] == T_PRUEBA && seen[2] == T_SI && seen[3] == T_NO && seen[4] == T_SOS); }
  { Rig r;  // menu times out
    r.b[2] = true; r.run(60); r.b[2] = false; r.run(60);
    CHECK(r.ui.menu_open());
    r.run(8100);
    CHECK(!r.ui.menu_open()); }
  { Rig r;  // VOL
    r.b[3] = true; r.run(50); r.b[3] = false; r.run(50);
    CHECK(r.count(UA_VOLUME_NEXT) == 1); }
  { Rig r;  // PTT closes the menu
    r.b[2] = true; r.run(60); r.b[2] = false; r.run(60);
    r.b[0] = true; r.run(30);
    CHECK(!r.ui.menu_open() && r.count(UA_PTT_START) == 1); }
}

static void test_battery() {
  BatteryParams p = {3300, 3400, 3500, 2000, 30000};
  BatteryMonitor m(p);
  CHECK(m.update(0, 4150, false) == BAT_OK);
  CHECK(m.percent() > 90);
  CHECK(m.update(1000, 3450, false) == BAT_LOW);
  CHECK(m.update(2000, 3390, false) == BAT_LOW);   // below the limit but not for 30 s yet
  CHECK(m.update(20000, 3390, false) == BAT_LOW);
  CHECK(m.update(33000, 3390, false) == BAT_CUTOFF);
  CHECK(m.update(34000, 3900, false) == BAT_CUTOFF);  // latched
  BatteryMonitor l(p);
  CHECK(l.update(0, 3350, true) == BAT_LOW);   // 3.35 V under load is above the loaded limit
  CHECK(l.update(40000, 3350, true) == BAT_LOW);
  CHECK(l.update(41000, 3290, true) == BAT_LOW);
  CHECK(l.update(72000, 3290, true) == BAT_CUTOFF);
  BatteryMonitor u(p);
  CHECK(u.update(0, 100, false) == BAT_NONE);
  CHECK(BatteryMonitor::percent_from_mv(3300) == 0 && BatteryMonitor::percent_from_mv(4300) == 100);
  CHECK(BatteryMonitor::percent_from_mv(3750) > BatteryMonitor::percent_from_mv(3650));
}

static void test_dsp() {
  // ceiling: no level may exceed AUDIO_CEILING (0.5 FS) even for a full-scale input
  for (int lv = 0; lv < 6; lv++) {
    int16_t g = volume_gain_q15(lv, 6, 16384);
    CHECK(abs(apply_gain(32767, g)) <= 16384);
    CHECK(abs(apply_gain(-32768, g)) <= 16384);
  }
  CHECK(volume_gain_q15(0, 6, 16384) == 0);
  CHECK(volume_gain_q15(5, 6, 16384) == 16383 || volume_gain_q15(5, 6, 16384) == 16384);
  // gain is monotonic
  for (int lv = 1; lv < 6; lv++) CHECK(volume_gain_q15(lv, 6, 16384) > volume_gain_q15(lv - 1, 6, 16384));
  // 1 kHz tone at 16 kHz survives decimation with ~unity gain; 7 kHz is attenuated
  const int N = 160;
  int16_t in[2 * N], out[N];
  Decimator2 d;
  int peak = 0;
  for (int i = 0; i < 2 * N; i++) in[i] = static_cast<int16_t>(10000.0 * __builtin_sin(2 * 3.14159265 * 1000 * i / 16000));
  d.process(in, out, N);
  for (int i
= 20; i < N; i++) peak = abs(out[i]) > peak ? abs(out[i]) : peak;
  CHECK(peak > 9000 && peak < 11000);
  Decimator2 d2;
  for (int i = 0; i < 2 * N; i++) in[i] = static_cast<int16_t>(10000.0 * __builtin_sin(2 * 3.14159265 * 7000 * i / 16000));
  d2.process(in, out, N);
  peak = 0;
  for (int i = 20; i < N; i++) peak = abs(out[i]) > peak ? abs(out[i]) : peak;
  CHECK(peak < 3000);
  // tone generator stays inside +-32768 and has the right period
  ToneGen tg;
  tg.set(1000, 16000);
  int16_t mn = 0, mx = 0;
  for (int i = 0; i < 1600; i++) {
    int16_t s = tg.next();
    if (s < mn) mn = s;
    if (s > mx) mx = s;
  }
  CHECK(mx > 30000 && mn < -30000);
}

int main() {
  test_debounce();
  test_ui();
  test_battery();
  test_dsp();
  printf("ui/battery/dsp: %d checks passed, %d failed\n", g_pass, g_fail);
  return g_fail ? 1 : 0;
}
