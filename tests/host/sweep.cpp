// Sweep of the SOS press instant during a clip (arbitration 2026-10-02, C Morty gate S1).
// For each scenario the SOS is pressed every 10 ms from 0 to 4000 ms after send_clip(); we measure
// press -> EV_MSG_ACKED at the node that pressed. Presses before the ANN has ended (< 260 ms) collide
// with the ANN (no LBT in v1) and are reported but not gated. FAILS (exit 1) if the worst case after the
// ANN is >= 3000 ms in the v1 scenarios (broadcast clip), or an alarm never reaches a node.
// SPDX-License-Identifier: GPL-3.0-or-later
#include <stdio.h>
#include <stdlib.h>

#include <algorithm>
#include <vector>

#include "sim.h"

using namespace sim;

static const int kAfterAnnMs = 260;
static const int kLimitMs = 3000;
static const int kUnicastLimitMs = 3700;  // documented limit: unicast clip + non-addressed third node

static std::vector<uint8_t> clipdata(int pairs) {
  std::vector<uint8_t> d(pairs * 13);
  for (size_t i = 0; i < d.size(); i++) d[i] = uint8_t(i * 7 + 1);
  return d;
}

// scen 0: broadcast clip, 2 nodes, the receiver presses SOS (v1 default)
// scen 1: broadcast clip, 3 nodes, one receiver presses SOS
// scen 2: unicast clip A->B, non-addressed C presses SOS
// scen 3: broadcast clip, 2 nodes, the clip sender presses SOS (control)
static int run1(int scen, int pairs, int off) {
  Net net;
  Node& a = net.add(1);
  Node& b = net.add(2);
  Node* c = (scen == 1 || scen == 2) ? &net.add(3) : nullptr;
  auto clip = clipdata(pairs);
  a.fsm.send_clip(scen == 2 ? 2 : NODE_BROADCAST, clip.data(), clip.size(), net.now);
  net.run(off);
  Node& s = scen == 3 ? a : (scen == 0 ? b : *c);
  uint32_t t0 = net.now;
  if (s.fsm.send_message(T_SOS, NODE_BROADCAST, net.now) != NodeFsm::SEND_OK) return -1;
  net.run_until([&] { return s.count(EV_MSG_ACKED) > 0; }, 30000);
  const TimedEvent* e = s.find(EV_MSG_ACKED);
  if (!e) return 99999;
  net.run(3000);
  for (auto& n : net.nodes)
    if (n.get() != &s && n->count(EV_MSG_RX) == 0) return 88888;  // someone never got the alarm
  return int(e->t - t0);
}

int main() {
  const int pairs = (int)lr::MAX_CLIP_PAIRS;
  const char* names[] = {"bcast 2 nodes, receiver SOS", "bcast 3 nodes, receiver SOS", "unicast, third-node SOS",
                         "clip sender SOS (control)"};
  int fails = 0;
  for (int scen = 0; scen < 4; scen++) {
    std::vector<int> v;
    int worst_after = 0, woff = 0, worst_all = 0, never = 0;
    for (int off = 0; off <= 4000; off += 10) {
      int d = run1(scen, pairs, off);
      if (d < 0) continue;
      v.push_back(d);
      if (d > worst_all) worst_all = d;
      if (off >= kAfterAnnMs) {
        if (d >= 88888) never++;
        else if (d > worst_after) {
          worst_after = d;
          woff = off;
        }
      }
    }
    std::sort(v.begin(), v.end());
    const int limit = scen == 2 ? kUnicastLimitMs : kLimitMs;
    const bool ok = worst_after < limit && never == 0;
    printf("[sweep] pairs=%d %-30s p50=%d p90=%d worst(after ANN)=%d ms @off=%d, never-delivered=%d, worst(all)=%d  %s (limit %d)\n",
           pairs, names[scen], v[v.size() / 2], v[v.size() * 9 / 10], worst_after, woff, never, worst_all,
           ok ? "OK" : "FAIL", limit);
    if (!ok) fails++;
  }
  return fails ? 1 : 0;
}
