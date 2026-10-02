// 4-button UI (PTT, SOS, MENU, VOL) turned into actions. Portable, host-tested.
//   PTT  hold = record, release = send clip
//   SOS  hold 1.5 s = SOS (a tap does nothing: no accidental alarms)
//   MENU tap = open / next item;  hold 1 s (menu open) = send highlighted item;  8 s idle = close
//   VOL  tap = next level 0..5 (cyclic)
//   any press silences a siren that is sounding (the notice stays on screen)
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <stdint.h>

#include "../protocol/protocol.h"

namespace lr {

enum UiActionKind : uint8_t {
  UA_NONE = 0,
  UA_PTT_START,
  UA_PTT_STOP,
  UA_SEND_MESSAGE,  // arg = packet type
  UA_VOLUME_NEXT,
  UA_ANY_PRESS,     // silence siren / wake the screen
  UA_MENU_CHANGED,  // menu opened, moved or closed: redraw
};

struct UiAction {
  UiActionKind kind = UA_NONE;
  uint8_t arg = 0;
};

struct UiConfig {
  uint32_t sos_hold_ms = 1500;
  uint32_t menu_long_ms = 1000;
  uint32_t menu_timeout_ms = 8000;
};

class UiLogic {
 public:
  static constexpr int MENU_ITEMS = 5;
  // Closed list of 5 messages. SOS is last so that scrolling past it needs deliberate presses.
  static uint8_t menu_type(int idx) {
    static const uint8_t items[MENU_ITEMS] = {T_PELIGRO, T_PRUEBA, T_SI, T_NO, T_SOS};
    return items[idx % MENU_ITEMS];
  }

  explicit UiLogic(const UiConfig& cfg = UiConfig()) : cfg_(cfg) {}

  // Call with debounced states every few ms. Collect actions with next().
  void update(uint32_t now, bool ptt, bool sos, bool menu, bool vol) {
    if ((ptt && !ptt_) || (sos && !sos_) || (menu && !menu_) || (vol && !vol_)) push({UA_ANY_PRESS, 0});

    if (ptt && !ptt_) {
      if (menu_open_) close_menu();
      push({UA_PTT_START, 0});
    }
    if (!ptt && ptt_) push({UA_PTT_STOP, 0});

    if (sos && !sos_) {
      sos_since_ = now;
      sos_fired_ = false;
    }
    if (sos && !sos_fired_ && static_cast<uint32_t>(now - sos_since_) >= cfg_.sos_hold_ms) {
      sos_fired_ = true;
      push({UA_SEND_MESSAGE, T_SOS});
    }

    if (menu && !menu_) {
      menu_since_ = now;
      menu_long_fired_ = false;
      was_open_on_press_ = menu_open_;
      menu_touch_ = now;
    }
    if (menu && menu_open_ && was_open_on_press_ && !menu_long_fired_ &&
        static_cast<uint32_t>(now - menu_since_) >= cfg_.menu_long_ms) {
      menu_long_fired_ = true;
      push({UA_SEND_MESSAGE, menu_type(menu_idx_)});
      close_menu();
    }
    if (!menu && menu_ && !menu_long_fired_) {  // released: it was a tap
      if (!menu_open_) {
        menu_open_ = true;
        menu_idx_ = 0;
      } else {
        menu_idx_ = (menu_idx_ + 1) % MENU_ITEMS;
      }
      menu_touch_ = now;
      push({UA_MENU_CHANGED, 0});
    }
    if (menu_open_ && !menu && static_cast<uint32_t>(now - menu_touch_) >= cfg_.menu_timeout_ms) close_menu();

    if (vol && !vol_) push({UA_VOLUME_NEXT, 0});

    ptt_ = ptt;
    sos_ = sos;
    menu_ = menu;
    vol_ = vol;
  }

  bool next(UiAction* out) {
    if (head_ == tail_) return false;
    *out = q_[head_];
    head_ = (head_ + 1) % QN;
    return true;
  }

  bool menu_open() const { return menu_open_; }
  int menu_index() const { return menu_idx_; }
  bool any_pressed() const { return ptt_ || sos_ || menu_ || vol_; }
  // 0..100 progress of the SOS hold, for the OLED
  int sos_progress(uint32_t now) const {
    if (!sos_) return 0;
    if (sos_fired_) return 100;
    uint32_t d = now - sos_since_;
    return d >= cfg_.sos_hold_ms ? 100 : static_cast<int>(d * 100 / cfg_.sos_hold_ms);
  }

 private:
  static constexpr int QN = 8;
  void push(UiAction a) {
    int n = (tail_ + 1) % QN;
    if (n == head_) return;  // full: drop (the UI is polled every few ms, cannot happen in practice)
    q_[tail_] = a;
    tail_ = n;
  }
  void close_menu() {
    if (menu_open_) {
      menu_open_ = false;
      push({UA_MENU_CHANGED, 0});
    }
  }

  UiConfig cfg_;
  UiAction q_[QN];
  int head_ = 0, tail_ = 0;
  bool ptt_ = false, sos_ = false, menu_ = false, vol_ = false;
  bool sos_fired_ = false, menu_long_fired_ = false, was_open_on_press_ = false, menu_open_ = false;
  uint32_t sos_since_ = 0, menu_since_ = 0, menu_touch_ = 0;
  int menu_idx_ = 0;
};

}  // namespace lr
