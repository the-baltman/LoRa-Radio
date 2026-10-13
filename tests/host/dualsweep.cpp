// Two SOS pressed almost at once, nodes idle (C Morty re-gate 2026-10-02, S10). Node A presses, B presses
// `off` ms later, off = 0..1500 ms every 10 ms, with kSeeds msg_id seeds (they seed the per-node retry backoff:
// one seed is one sample of the random delay; 151 x 16 = 2416 cases per topology); with 3 nodes the third one
// only listens. A case is delivered when both SOS are acknowledged and every node got every alarm it did not
// send. FAILS (exit 1) on any case not fully delivered, or if the worst press -> ACK of either sender reaches
// kLimitMs. LR_SEEDS=N (env) runs N seeds instead of the default 16 (the 16 listed, then generated ones).
// Known (2026-10-12): with LR_SEEDS=256 the 2-node case leaves ~0.3 % of cases undelivered (both SOS FAILED
// after 10 tries): adjacent backoff slots (268 ms) overlap a 248 ms SOS once the two nodes are ~20 ms apart.
// SPDX-License-Identifier: GPL-3.0-or-later
#include <stdio.h>
#include <stdlib.h>

#include <algorithm>
#include <vector>

#include "sim.h"

using namespace sim;
using namespace lr;

static const int kLimitMs = 13000;  // re-gate 2 D1: up to 12.1 s seen with 2 nodes over 16 seeds
static const uint16_t kSeedList[] = {100,  7777,  31337, 1,     42,    255,   1000,  4242,
                                     9999, 12345, 20000, 27182, 31415, 40000, 50000, 65000};

int main() {
  const int n_list = sizeof kSeedList / sizeof kSeedList[0];
  const char* env = getenv("LR_SEEDS");
  int n_seeds = env ? atoi(env) : n_list;
  if (n_seeds < 1) n_seeds = n_list;
  std::vector<uint16_t> kSeeds;
  for (int k = 0; k < n_seeds; k++) kSeeds.push_back(k < n_list ? kSeedList[k] : uint16_t(k * 40503u + 17u));
  int fails = 0;
  for (int nn : {2, 3}) {
    std::vector<int> v;
    int never = 0, worst_off = 0, worst_seed = 0;
    for (int off = 0; off <= 1500; off += 10) {
      for (uint16_t seed : kSeeds) {
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
        if (v.empty() || d > *std::max_element(v.begin(), v.end())) {
          worst_off = off;
          worst_seed = seed;
        }
        v.push_back(d);
      }
    }
    std::sort(v.begin(), v.end());
    const int worst = v.empty() ? 99999 : v.back();
    const bool ok = never == 0 && worst < kLimitMs;
    printf("[dualsweep] %d nodes, offsets 0-1500 ms x %zu seeds: n=%zu p50=%d p90=%d p99=%d max=%d ms @off=%d seed=%d, not delivered=%d  %s (limit %d)\n",
           nn, kSeeds.size(), v.size(), v.empty() ? 0 : v[v.size() / 2],
           v.empty() ? 0 : v[v.size() * 9 / 10], v.empty() ? 0 : v[v.size() * 99 / 100], worst, worst_off, worst_seed,
           never, ok ? "OK" : "FAIL", kLimitMs);
    if (!ok) fails++;
  }
  return fails ? 1 : 0;
}
