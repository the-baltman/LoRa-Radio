// Failure injection on the receiver-SOS-during-clip path (C Morty re-gate 2026-10-02, S9).
// Every case must end with: the SOS transmitted or a FAILED event, the node out of VRX, and the SOS button
// alive (a new press gives SEND_OK, never SEND_ALREADY). FAILS (exit 1) otherwise.
// SPDX-License-Identifier: GPL-3.0-or-later
#include <stdio.h>

#include <vector>

#include "sim.h"

using namespace sim;
using namespace lr;

static int g_fail = 0, g_checks = 0;
#define EXPECT(c)                                              \
  do {                                                         \
    g_checks++;                                                \
    if (!(c)) {                                                \
      printf("    FAIL %s:%d  %s\n", __FILE__, __LINE__, #c); \
      g_fail++;                                                \
    }                                                          \
  } while (0)

static std::vector<uint8_t> clipdata(int pairs) {
  std::vector<uint8_t> d(pairs * PAIR_BYTES);
  for (size_t i = 0; i < d.size(); i++) d[i] = uint8_t(i * 7 + 1);
  return d;
}
static int sos_tx_by(const Net& net, int index) {
  int n = 0;
  for (auto& r : net.log) n += (r.from == index && (r.buf[2] & 0x0F) == T_SOS);
  return n;
}
static bool idle(const Node& n) { return n.fsm.role() == NodeFsm::Role::NONE; }

int main() {
  const int pairs = (int)MAX_CLIP_PAIRS;
  {  // E1: the receiver's radio refuses TX for 1.5 s around its ACK_SOS slot (transient)
    Net net;
    Node& a = net.add(1);
    Node& b = net.add(2);
    auto c = clipdata(pairs);
    a.fsm.send_clip(NODE_BROADCAST, c.data(), c.size(), net.now);
    net.run(900);
    EXPECT(b.fsm.send_message(T_SOS, NODE_BROADCAST, net.now) == NodeFsm::SEND_OK);
    net.run(1000);
    b.radio_fail = true;
    net.run(1500);
    b.radio_fail = false;
    net.run(20000);
    printf("E1 ACK_SOS slot refused: b idle=%d SOS tx=%d a got=%d b acked=%d\n", idle(b), sos_tx_by(net, 1),
           a.count(EV_MSG_RX), b.count(EV_MSG_ACKED));
    EXPECT(idle(b));
    EXPECT(sos_tx_by(net, 1) >= 1);
    EXPECT(a.count(EV_MSG_RX) >= 1);
    EXPECT(b.count(EV_MSG_ACKED) == 1);
    EXPECT(b.fsm.send_message(T_SOS, NODE_BROADCAST, net.now) == NodeFsm::SEND_OK);
  }
  {  // E1b: the receiver's radio is dead from the press on: no hang in VRX, the SOS ends in FAILED
    Net net;
    Node& a = net.add(1);
    Node& b = net.add(2);
    auto c = clipdata(pairs);
    a.fsm.send_clip(NODE_BROADCAST, c.data(), c.size(), net.now);
    net.run(900);
    b.radio_fail = true;
    EXPECT(b.fsm.send_message(T_SOS, NODE_BROADCAST, net.now) == NodeFsm::SEND_OK);
    net.run(20000);
    printf("E1b radio dead: b idle=%d failed=%d\n", idle(b), b.count(EV_MSG_FAILED));
    EXPECT(idle(b));
    EXPECT(b.count(EV_MSG_FAILED) == 1);
    b.radio_fail = false;
    EXPECT(b.fsm.send_message(T_SOS, NODE_BROADCAST, net.now) == NodeFsm::SEND_OK);
  }
  {  // E2: receiver presses SOS, then the clip sender presses SOS before the slot (VABORT): both delivered
    Net net;
    Node& a = net.add(1);
    Node& b = net.add(2);
    auto c = clipdata(pairs);
    a.fsm.send_clip(NODE_BROADCAST, c.data(), c.size(), net.now);
    net.run(900);
    EXPECT(b.fsm.send_message(T_SOS, NODE_BROADCAST, net.now) == NodeFsm::SEND_OK);
    net.run(100);
    EXPECT(a.fsm.send_message(T_SOS, NODE_BROADCAST, net.now) == NodeFsm::SEND_OK);
    net.run(20000);
    printf("E2 VABORT before slot: b idle=%d b SOS tx=%d a got=%d b got=%d a acked=%d b acked=%d\n", idle(b),
           sos_tx_by(net, 1), a.count(EV_MSG_RX), b.count(EV_MSG_RX), a.count(EV_MSG_ACKED), b.count(EV_MSG_ACKED));
    EXPECT(idle(a) && idle(b));
    EXPECT(a.count(EV_MSG_RX) >= 1 && b.count(EV_MSG_RX) >= 1);
    EXPECT(a.count(EV_MSG_ACKED) == 1 && b.count(EV_MSG_ACKED) == 1);
    EXPECT(b.fsm.send_message(T_SOS, NODE_BROADCAST, net.now) == NodeFsm::SEND_OK);
  }
  {  // E3: link lost right after the press: 10 tries, FAILED, button alive
    Net net;
    Node& a = net.add(1);
    Node& b = net.add(2);
    auto c = clipdata(pairs);
    a.fsm.send_clip(NODE_BROADCAST, c.data(), c.size(), net.now);
    net.run(900);
    net.drop = [](const TxRec&, int) { return true; };
    EXPECT(b.fsm.send_message(T_SOS, NODE_BROADCAST, net.now) == NodeFsm::SEND_OK);
    net.run(30000);
    printf("E3 link lost: b idle=%d SOS tx=%d failed=%d\n", idle(b), sos_tx_by(net, 1), b.count(EV_MSG_FAILED));
    EXPECT(idle(b));
    EXPECT(sos_tx_by(net, 1) == MAX_TRIES_SOS);
    EXPECT(b.count(EV_MSG_FAILED) == 1);
    EXPECT(b.fsm.send_message(T_SOS, NODE_BROADCAST, net.now) == NodeFsm::SEND_OK);
  }
  {  // E4: SOS pressed while the receiver is closing (clip done, final ACK queued) and that ACK is refused
    Net net;
    Node& a = net.add(1);
    Node& b = net.add(2);
    auto c = clipdata(pairs);
    a.fsm.send_clip(NODE_BROADCAST, c.data(), c.size(), net.now);
    EXPECT(net.run_until([&] { return b.count(EV_CLIP_RX_DONE) > 0; }, 10000));
    b.radio_fail = true;
    EXPECT(b.fsm.send_message(T_SOS, NODE_BROADCAST, net.now) == NodeFsm::SEND_OK);
    net.run(150);
    b.radio_fail = false;
    net.run(20000);
    printf("E4 SOS while closing, final ACK refused: b idle=%d SOS tx=%d a got=%d b acked=%d rx_abort=%d\n", idle(b),
           sos_tx_by(net, 1), a.count(EV_MSG_RX), b.count(EV_MSG_ACKED), b.count(EV_CLIP_RX_ABORT));
    EXPECT(idle(b));
    EXPECT(a.count(EV_MSG_RX) >= 1);
    EXPECT(b.count(EV_MSG_ACKED) == 1);
    EXPECT(b.count(EV_CLIP_RX_ABORT) == 0);  // the clip was delivered: no "CLIP DESCARTADO"
    EXPECT(b.fsm.send_message(T_SOS, NODE_BROADCAST, net.now) == NodeFsm::SEND_OK);
  }
  printf("[edge] %d checks, %d failed  %s\n", g_checks, g_fail, g_fail ? "FAIL" : "OK");
  return g_fail ? 1 : 0;
}
