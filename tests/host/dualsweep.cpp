// Two SOS pressed almost at once, nodes idle (C Morty re-gate 2026-10-02, S10). Node A presses, B presses
// `off` ms later, off = 0..1500 ms every 10 ms, with 3 msg_id seeds (453 cases per topology); with 3 nodes
// the third one only listens. A case is delivered when both SOS are acknowledged and every node got every
// alarm it did not send. FAILS (exit 1) on any case not fully delivered, or if the worst press -> ACK of
// either sender reaches kLimitMs.
// SPDX-License-Identifier: GPL-3.0-or-later
#include <stdio.h>

#include <algorithm>
#include <vector>

#include "sim.h"

using namespace sim;
using namespace lr;

static const int kLimitMs = 8000;  // documented limit; prototype worst case 7.2 s

int main() {
  int fails = 0;
  for (int nn : {2, 3}) {
    std::vector<int> v;
    int never = 0, worst_off = 0;
    for (int off = 0; off <= 1500; off += 10) {
      for (uint16_t seed : {100, 7777, 31337}) {
        Net net;
        Node& a = net.add(1, seed);
        Node& b = net.add(2, static_cast<uint16_t>(seed + 1));
        Node* c = nn == 3 ? &net.add(3, static_cast<uint16_t>(seed + 2)) : nullptr;
        const uint32_t ta = net.now;
        a.fsm.send_message(T_SOS, NODE_BROADCAST, net.now);
        net.run(off);
        const uint32_t tb = net.now;
        b.fsm.send_message(T_SOS, NODE_BROADCAST, net.now);
        net.run_until([&] { return a.count(EV_MSG_ACKED) && b.count(EV_MSG_ACKED); }, 30000);
        net.run(3000);
        const bool all = a.count(EV_MSG_RX) >= 1 && b.count(EV_MSG_RX) >= 1 && (!c || c->count(EV_MSG_RX) >= 2);
        if (!a.count(EV_MSG_ACKED) || !b.count(EV_MSG_ACKED) || !all) {
          never++;
          continue;
        }
        const int d = (int)std::max(a.find(EV_MSG_ACKED)->t - ta, b.find(EV_MSG_ACKED)->t - tb);
        if (v.empty() || d > *std::max_element(v.begin(), v.end())) worst_off = off;
        v.push_back(d);
      }
    }
    std::sort(v.begin(), v.end());
    const int worst = v.empty() ? 99999 : v.back();
    const bool ok = never == 0 && worst < kLimitMs;
    printf("[dualsweep] %d nodes, offsets 0-1500 ms x 3 seeds: n=%zu p50=%d p90=%d max=%d ms @off=%d, not delivered=%d  %s (limit %d)\n",
           nn, v.size(), v.empty() ? 0 : v[v.size() / 2], v.empty() ? 0 : v[v.size() * 9 / 10], worst, worst_off,
           never, ok ? "OK" : "FAIL", kLimitMs);
    if (!ok) fails++;
  }
  return fails ? 1 : 0;
}
