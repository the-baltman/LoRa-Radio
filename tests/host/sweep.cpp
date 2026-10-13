// Sweep of the SOS press instant during a clip (arbitration 2026-10-02, C Morty gate S1; seeds: re-gate 2 D1).
// For each scenario the SOS is pressed every 10 ms from 0 to 4000 ms after send_clip(), with kSeeds different
// first msg_ids (they seed the per-node retry backoff, so one seed is one sample of the random delay); we
// measure press -> EV_MSG_ACKED at the node that pressed. Presses before the ANN has ended (< 260 ms) collide
// with the ANN (no LBT in v1) and are reported but not gated. FAILS (exit 1) if, after the ANN, a node never
// gets the alarm or the worst case reaches the scenario limit:
//   receiver SOS (v1 broadcast, 2 and 3 nodes) < 3000 ms
//   clip sender SOS < 3200 ms (arbitration 2026-10-12: up to 3139 ms when pressed right after its own burst)
// The unicast clip + non-addressed third node (future function, not reachable in v1) is printed, not gated:
// its worst case is 4628 ms for every seed set tried (16 and 256 seeds).
// LR_SEEDS=N (env) runs N seeds instead of the default 16 (the 16 listed, then generated ones).
// SPDX-License-Identifier: GPL-3.0-or-later
#include <stdio.h>
#include <stdlib.h>

#include <algorithm>
#include <vector>

#include "sim.h"

using namespace sim;

static const int kAfterAnnMs = 260;
static const uint16_t kSeedList[] = {100,  7777,  31337, 1,     42,    255,   1000,  4242,
                                     9999, 12345, 20000, 27182, 31415, 40000, 50000, 65000};

static std::vector<uint16_t> seeds() {
  const int n_list = sizeof kSeedList / sizeof kSeedList[0];
  const char* env = getenv("LR_SEEDS");
  int n = env ? atoi(env) : n_list;
  if (n < 1) n = n_list;
  std::vector<uint16_t> s;
  for (int k = 0; k < n; k++) s.push_back(k < n_list ? kSeedList[k] : uint16_t(k * 40503u + 17u));
  return s;
}

static std::vector<uint8_t> clipdata(int pairs) {
  std::vector<uint8_t> d(pairs * 13);
  for (size_t i = 0; i < d.size(); i++) d[i] = uint8_t(i * 7 + 1);
  return d;
}

// scen 0: broadcast clip, 2 nodes, the receiver presses SOS (v1 default)
// scen 1: broadcast clip, 3 nodes, one receiver presses SOS
// scen 2: unicast clip A->B, non-addressed C presses SOS (future function)
// scen 3: broadcast clip, 2 nodes, the clip sender presses SOS (control)
static int run1(int scen, int pairs, int off, uint16_t seed) {
  Net net;
  Node& a = net.add(1, seed);
  Node& b = net.add(2, seed);
  Node* c = (scen == 1 || scen == 2) ? &net.add(3, seed) : nullptr;
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
  const char* names[] = {"bcast 2 nodes, receiver SOS", "bcast 3 nodes, receiver SOS", "unicast, third-node SOS (future)",
                         "clip sender SOS (control)"};
  const int limits[] = {3000, 3000, 0, 3200};  // 0 = informative, not gated
  const std::vector<uint16_t> sd = seeds();
  int fails = 0;
  for (int scen = 0; scen < 4; scen++) {
    std::vector<int> v;
    int worst_after = 0, woff = 0, wseed = 0, never = 0, over3000 = 0, n_after = 0, worst_pre = 0, pre_missing = 0;
    for (uint16_t seed : sd) {
      for (int off = 0; off <= 4000; off += 10) {
        int d = run1(scen, pairs, off, seed);
        if (d < 0) continue;
        if (off < kAfterAnnMs) {
          if (d >= 88888) pre_missing++;
          else if (d > worst_pre) worst_pre = d;
          continue;
        }
        n_after++;
        if (d >= 88888) {
          never++;
          continue;
        }
        v.push_back(d);
        if (d >= 3000) over3000++;
        if (d > worst_after) {
          worst_after = d;
          woff = off;
          wseed = seed;
        }
      }
    }
    std::sort(v.begin(), v.end());
    const bool gated = limits[scen] > 0;
    const bool ok = (!gated || worst_after < limits[scen]) && never == 0;
    printf("[sweep] pairs=%d seeds=%zu %-33s after ANN: n=%d p50=%d p90=%d worst=%d ms @off=%d seed=%u, >=3000 ms: %d, "
           "never-delivered=%d | before ANN: worst delivered=%d ms, node without alarm=%d  %s",
           pairs, sd.size(), names[scen], n_after, v.empty() ? 0 : v[v.size() / 2], v.empty() ? 0 : v[v.size() * 9 / 10],
           worst_after, woff, (unsigned)wseed, over3000, never, worst_pre, pre_missing, ok ? "OK" : "FAIL");
    if (gated) printf(" (limit %d)\n", limits[scen]);
    else printf(" (informative, not gated)\n");
    if (!ok) fails++;
  }
  return fails ? 1 : 0;
}
