// Discrete-event radio simulator for NodeFsm tests: 1 ms resolution, real time-on-air, one SF/BW at a
// time per node (a node only hears packets sent in the profile it was tuned to for the whole packet).
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <functional>
#include <memory>
#include <vector>

#include "protocol/node_fsm.h"

namespace sim {
using namespace lr;

struct TimedEvent {
  uint32_t t;
  Event e;
};

struct TxRec {
  int from;
  uint32_t start, end;
  ProfileId prof;
  std::vector<uint8_t> buf;
};

class Net;

struct Node : public Host {
  Node(Net* n, int idx, uint8_t id, uint16_t first_msg) : net(n), index(idx), fsm(*this, id, first_msg) {}
  void radio_set_profile(ProfileId p) override;
  bool radio_tx(const uint8_t* buf, size_t len) override;
  void notify(const Event& e) override;

  Net* net;
  int index;
  NodeFsm fsm;
  ProfileId prof = PROFILE_ALARM;
  uint32_t prof_since = 0;
  bool transmitting = false;
  bool radio_fail = false;  // radio_tx() refuses
  std::vector<TimedEvent> events;
  int count(EventKind k) const {
    int n = 0;
    for (auto& te : events) n += (te.e.kind == k);
    return n;
  }
  const TimedEvent* find(EventKind k, int nth = 0) const {
    for (auto& te : events)
      if (te.e.kind == k && nth-- == 0) return &te;
    return nullptr;
  }
};

class Net {
 public:
  uint32_t now = 0;
  std::vector<std::unique_ptr<Node>> nodes;
  std::vector<TxRec> inflight;
  std::vector<TxRec> log;
  // return true to DROP the packet for receiver `rx`
  std::function<bool(const TxRec&, int rx)> drop;
  float rssi = -90.0f, snr = 5.0f;

  Node& add(uint8_t id, uint16_t first_msg = 100) {
    nodes.emplace_back(new Node(this, static_cast<int>(nodes.size()), id, first_msg));
    nodes.back()->fsm.begin(0);
    return *nodes.back();
  }

  int count_tx(PacketType t) const {
    int n = 0;
    for (auto& r : log) n += ((r.buf[2] & 0x0F) == t);
    return n;
  }

  void step() {
    now++;
    for (size_t i = 0; i < inflight.size();) {
      if (inflight[i].end != now) {
        i++;
        continue;
      }
      TxRec r = inflight[i];
      inflight.erase(inflight.begin() + i);
      log.push_back(r);
      for (auto& n : nodes) {
        if (n->index == r.from || n->transmitting || n->prof != r.prof || n->prof_since > r.start) continue;
        bool collided = false;
        for (auto& o : inflight)
          if (o.prof == r.prof && o.start < r.end && o.end > r.start) collided = true;
        for (auto& o : log)
          if (&o != &log.back() && o.prof == r.prof && o.from != r.from && o.start < r.end && o.end > r.start)
            collided = true;
        if (collided) continue;
        if (drop && drop(r, n->index)) continue;
        n->fsm.on_rx(r.buf.data(), r.buf.size(), rssi, snr, now);
      }
      nodes[r.from]->transmitting = false;
      nodes[r.from]->fsm.on_tx_done(now);
    }
    for (auto& n : nodes) n->fsm.tick(now);
  }

  void run(uint32_t ms) {
    for (uint32_t i = 0; i < ms; i++) step();
  }
  bool run_until(const std::function<bool()>& cond, uint32_t max_ms) {
    for (uint32_t i = 0; i < max_ms; i++) {
      if (cond()) return true;
      step();
    }
    return cond();
  }
};

inline void Node::radio_set_profile(ProfileId p) {
  prof = p;
  prof_since = net->now;
}
inline bool Node::radio_tx(const uint8_t* buf, size_t len) {
  if (radio_fail || transmitting) return false;
  transmitting = true;
  TxRec r{index, net->now, net->now + toa_ms(prof, len), prof, std::vector<uint8_t>(buf, buf + len)};
  net->inflight.push_back(r);
  return true;
}
inline void Node::notify(const Event& e) { events.push_back({net->now, e}); }

}  // namespace sim
