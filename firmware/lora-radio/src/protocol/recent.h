// Duplicate filter for retried messages: a sender repeats the SAME msg_id until it hears an ACK,
// so the receiver must re-ACK but must not re-raise the alarm. Entries expire so that a reboot
// (counter restarts) cannot be mistaken for a duplicate forever.
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <stdint.h>

namespace lr {

class RecentTable {
 public:
  static constexpr int SIZE = 8;
  static constexpr uint32_t TTL_MS = 30000;  // > longest SOS retry train (see node_fsm.h)

  // Records (src, msg_id, type); returns true if it was already there and still fresh.
  bool seen_and_record(uint8_t src, uint16_t msg_id, uint8_t type, uint32_t now) {
    int free_slot = -1, oldest = 0;
    for (int i = 0; i < SIZE; i++) {
      Entry& e = e_[i];
      if (e.used && (now - e.t) >= TTL_MS) e.used = false;
      if (e.used && e.src == src && e.id == msg_id && e.type == type) {
        e.t = now;
        return true;
      }
      if (!e.used && free_slot < 0) free_slot = i;
      if (e.used && (now - e.t) > (now - e_[oldest].t)) oldest = i;
    }
    int slot = (free_slot >= 0) ? free_slot : oldest;
    e_[slot] = Entry{true, src, type, msg_id, now};
    return false;
  }

 private:
  struct Entry {
    bool used;
    uint8_t src;
    uint8_t type;
    uint16_t id;
    uint32_t t;
  };
  Entry e_[SIZE] = {};
};

}  // namespace lr
