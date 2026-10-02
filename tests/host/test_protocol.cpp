// Host tests: packet codec, airtime, pair packing, reassembly, counters, state machine.
// Build/run: tests/host/run.sh   SPDX-License-Identifier: GPL-3.0-or-later
#include <math.h>
#include <stdio.h>
#include <string.h>

#include "audio/pair_pack.h"
#include "sim.h"

using namespace lr;
using namespace sim;

static int g_fail = 0, g_pass = 0;
#define CHECK(c)                                                           \
  do {                                                                     \
    if (c) g_pass++;                                                       \
    else {                                                                 \
      g_fail++;                                                            \
      printf("  FAIL %s:%d  %s\n", __FILE__, __LINE__, #c);                \
    }                                                                      \
  } while (0)
#define TEST(name) static void name()
#define RUN(name)                \
  do {                           \
    printf("[ RUN ] %s\n", #name); \
    name();                      \
  } while (0)

static std::vector<uint8_t> make_clip(int pairs) {
  std::vector<uint8_t> d(pairs * PAIR_BYTES);
  for (size_t i = 0; i < d.size(); i++) d[i] = static_cast<uint8_t>(i * 31 + 7);
  return d;
}

// ---------------------------------------------------------------------------------------------
TEST(codec_roundtrip_and_rejects) {
  uint8_t b[MAX_PACKET];
  size_t n = build_message(b, T_SOS, 2, NODE_BROADCAST, 0xBEEF);
  CHECK(n == 8);
  Packet p;
  CHECK(parse_packet(b, n, &p) == PARSE_OK);
  CHECK(p.h.type == T_SOS && p.h.src == 2 && p.h.dst == 0xFF && p.h.msg_id == 0xBEEF);

  uint8_t bad[MAX_PACKET];
  memcpy(bad, b, n);
  bad[0] ^= 1;
  CHECK(parse_packet(bad, n, &p) == PARSE_BAD_NET);
  memcpy(bad, b, n);
  bad[2] = (2 << 4) | T_SOS;
  CHECK(parse_packet(bad, n, &p) == PARSE_BAD_VERSION);
  memcpy(bad, b, n);
  bad[2] = (1 << 4) | 15;
  CHECK(parse_packet(bad, n, &p) == PARSE_BAD_TYPE);
  memcpy(bad, b, n);
  bad[3] = 0;
  CHECK(parse_packet(bad, n, &p) == PARSE_BAD_ADDR);
  CHECK(parse_packet(b, 7, &p) == PARSE_TOO_SHORT);

  n = build_ack(b, 1, 2, 77, T_PRUEBA, ACK_OK, -92.4f, 7.5f);
  CHECK(parse_packet(b, n, &p) == PARSE_OK);
  AckInfo a;
  CHECK(parse_ack(p, &a) && a.acked_type == T_PRUEBA && a.rssi_dbm == -92 && fabsf(a.snr_db - 7.5f) < 0.01f);
  n = build_ack(b, 1, 2, 77, T_PRUEBA, ACK_OK, -92.6f, -12.25f);
  parse_packet(b, n, &p);
  parse_ack(p, &a);
  CHECK(a.rssi_dbm == -93 && fabsf(a.snr_db + 12.25f) < 0.01f);
  CHECK(build_ack(b, 1, NODE_BROADCAST, 1, T_SOS, 0, 0, 0) == 0);  // ACK to broadcast is malformed

  n = build_nack(b, 2, 1, 5, 0x0A04);
  uint16_t m;
  parse_packet(b, n, &p);
  CHECK(parse_nack(p, &m) && m == 0x0A04);

  n = build_vann(b, 1, 0xFF, 9, PROFILE_VOICE_C2_1300, 7, 125);
  parse_packet(b, n, &p);
  VannInfo v;
  CHECK(parse_vann(p, &v) && v.frag_cnt == 7 && v.pairs == 125 && n == 12);

  std::vector<uint8_t> audio = make_clip(FRAG_PAIRS);
  n = build_vfrag(b, 1, 2, 9, 3, 7, audio.data(), FRAG_PAIRS);
  CHECK(n == 255);
  CHECK(parse_packet(b, n, &p) == PARSE_OK && p.h.frag_idx == 3 && p.h.frag_cnt == 7 && p.plen == 247);
  CHECK(build_vfrag(b, 1, 2, 9, 7, 7, audio.data(), 1) == 0);   // idx >= cnt
  CHECK(build_vfrag(b, 1, 2, 9, 0, 7, audio.data(), 20) == 0);  // too many pairs
  b[HEADER_LEN + 0] = 0;
  memcpy(bad, b, 100);
  CHECK(parse_packet(bad, 8 + 14, &p) == PARSE_BAD_PAYLOAD);    // not a multiple of 13
}

TEST(airtime_matches_scientist) {
  // Ciencia\...\LoRa-Radio.md sec. 2.2 and 4.2
  CHECK(fabsf(toa_ms_f(PARAMS_VOICE, 255) - 312.6f) < 0.5f);
  CHECK(fabsf(toa_ms_f(PARAMS_ALARM, 255) - 1927.2f) < 0.5f);
  CHECK(toa_ms(PROFILE_ALARM, 12) == 248);
  CHECK(toa_ms(PROFILE_ALARM, 8) == 248);
  LoraParams sf7 = {7, 125000, 1, 8};
  CHECK(fabsf(toa_ms_f(sf7, 255) - 399.6f) < 0.5f);
}

TEST(pair_pack_roundtrip) {
  for (int seed = 0; seed < 200; seed++) {
    uint8_t a[7], b[7], out[13], a2[7], b2[7];
    for (int i = 0; i < 7; i++) {
      a[i] = static_cast<uint8_t>(seed * 13 + i * 71);
      b[i] = static_cast<uint8_t>(seed * 29 + i * 17 + 5);
    }
    a[6] &= 0xF0;  // codec2 leaves the padding nibble zero
    b[6] &= 0xF0;
    pack_pair(a, b, out);
    unpack_pair(out, a2, b2);
    CHECK(memcmp(a, a2, 7) == 0 && memcmp(b, b2, 7) == 0);
  }
}

TEST(clip_rx_reassembly) {
  ClipRx c;
  CHECK(!c.begin(2, 1, 9, 125));  // wrong fragment count for the pairs
  CHECK(!c.begin(2, 1, 9, 171));  // more than the buffer holds
  CHECK(c.begin(2, 1, 7, 125));
  auto clip = make_clip(125);
  CHECK(frag_pairs(125, 6) == 11 && frag_pairs(125, 0) == 19 && frag_count_for(125) == 7);
  int order[] = {3, 0, 6, 1, 1, 5, 2};
  int dups = 0;
  for (int idx : order) {
    uint8_t pr = static_cast<uint8_t>(frag_pairs(125, idx));
    auto r = c.add(idx, clip.data() + idx * FRAG_AUDIO_BYTES, pr);
    if (r == ClipRx::ADD_DUP) dups++;
  }
  CHECK(dups == 1);
  CHECK(c.missing_mask() == (1u << 4));
  CHECK(!c.complete());
  CHECK(c.add(4, clip.data() + 4 * FRAG_AUDIO_BYTES, 19) == ClipRx::ADD_NEW);
  CHECK(c.add(4, clip.data() + 4 * FRAG_AUDIO_BYTES, 19) == ClipRx::ADD_DUP);
  CHECK(c.complete());
  CHECK(memcmp(c.data(), clip.data(), clip.size()) == 0);
  ClipRx d;
  d.begin(2, 1, 7, 125);
  CHECK(d.add(6, clip.data(), 19) == ClipRx::ADD_REJECT);  // last fragment must carry 11 pairs
  CHECK(d.add(9, clip.data(), 19) == ClipRx::ADD_REJECT);
}

TEST(counters_and_wraparound) {
  CHECK(seq_gap(10, 11) == 0);
  CHECK(seq_gap(10, 14) == 3);
  CHECK(seq_gap(65535, 0) == 0);
  CHECK(seq_gap(65534, 1) == 2);
  CHECK(seq_gap(10, 10) == -1);
  CHECK(seq_gap(10, 9) == -1);

  Net net;
  Node& a = net.add(1, 65533);
  Node& b = net.add(2);
  int sent = 0;
  // drop id 65535: the receiver must count exactly 1 lost across the 65535 -> 0 wrap
  net.drop = [&](const TxRec& r, int) { return (r.buf[2] & 0x0F) == T_PRUEBA && (r.buf[5] | r.buf[6] << 8) == 65535; };
  for (int i = 0; i < 6; i++) {
    CHECK(a.fsm.send_message(T_PRUEBA, 2, net.now) == NodeFsm::SEND_OK);
    sent++;
    net.run_until([&] { return !a.fsm.message_pending(); }, 3000);
    net.run(100);
  }
  const NodeFsm::PeerStat* ps = b.fsm.peer(1);
  CHECK(ps != nullptr);
  if (ps) {
    CHECK(ps->rx == 5);    // id 65535 dropped
    CHECK(ps->lost == 1);
  }
  CHECK(a.count(EV_MSG_FAILED) == 1 && a.count(EV_MSG_ACKED) == 5);
}

TEST(message_ack_cycle_time) {
  Net net;
  Node& a = net.add(1);
  Node& b = net.add(2);
  uint32_t t0 = net.now;
  a.fsm.send_message(T_SOS, NODE_BROADCAST, net.now);
  net.run_until([&] { return a.count(EV_MSG_ACKED) > 0; }, 5000);
  CHECK(a.count(EV_MSG_ACKED) == 1);
  CHECK(b.count(EV_MSG_RX) == 1);
  const TimedEvent* ack = a.find(EV_MSG_ACKED);
  uint32_t cycle = ack->t - t0;
  printf("  SOS broadcast 1->2 + ACK cycle: %u ms\n", cycle);
  CHECK(cycle < 1000);  // scientist target <= 1.0 s
  CHECK(b.find(EV_MSG_RX)->e.type == T_SOS && b.find(EV_MSG_RX)->e.src == 1);
  CHECK(ack->e.tries == 1 && ack->e.src == 2);
  // reverse direction: node 2 has ACK slot 1 only when it is the acker
  uint32_t t1 = net.now;
  b.fsm.send_message(T_SOS, NODE_BROADCAST, net.now);
  net.run_until([&] { return b.count(EV_MSG_ACKED) > 0; }, 5000);
  printf("  SOS broadcast 2->1 + ACK cycle: %u ms\n", b.find(EV_MSG_ACKED)->t - t1);
  CHECK(b.find(EV_MSG_ACKED)->t - t1 < 1000);
}

TEST(prueba_reports_rssi_snr_and_is_silent) {
  Net net;
  net.rssi = -101.0f;
  net.snr = -3.5f;
  Node& a = net.add(1);
  Node& b = net.add(2);
  a.fsm.send_message(T_PRUEBA, 2, net.now);
  net.run_until([&] { return a.count(EV_MSG_ACKED) > 0; }, 3000);
  const Event& rx = b.find(EV_MSG_RX)->e;
  CHECK(rx.type == T_PRUEBA && rx.rssi == -101.0f && rx.snr == -3.5f);  // UI decides: PRUEBA = no siren
  const Event& ak = a.find(EV_MSG_ACKED)->e;
  CHECK(ak.rssi_remote == -101.0f && fabsf(ak.snr_remote + 3.5f) < 0.01f);
}

TEST(ack_lost_retry_no_double_alarm) {
  Net net;
  Node& a = net.add(1);
  Node& b = net.add(2);
  int acks_dropped = 0;
  net.drop = [&](const TxRec& r, int) {
    if ((r.buf[2] & 0x0F) == T_ACK && acks_dropped < 1) {
      acks_dropped++;
      return true;
    }
    return false;
  };
  a.fsm.send_message(T_SOS, NODE_BROADCAST, net.now);
  net.run_until([&] { return a.count(EV_MSG_ACKED) > 0; }, 6000);
  CHECK(a.find(EV_MSG_ACKED) && a.find(EV_MSG_ACKED)->e.tries == 2);
  CHECK(b.count(EV_MSG_RX) == 1);  // retry is a duplicate: ACKed again, alarm not raised again
  CHECK(b.fsm.stats().rx_dup == 1);
}

TEST(message_gives_up_without_peer) {
  Net net;
  Node& a = net.add(1);
  net.add(2);
  net.drop = [](const TxRec&, int) { return true; };
  a.fsm.send_message(T_PELIGRO, NODE_BROADCAST, net.now);
  net.run_until([&] { return a.count(EV_MSG_FAILED) > 0; }, 20000);
  CHECK(a.find(EV_MSG_FAILED) && a.find(EV_MSG_FAILED)->e.tries == MAX_TRIES_DEFAULT);
  CHECK(!a.fsm.message_pending());
  a.fsm.send_message(T_SOS, NODE_BROADCAST, net.now);
  net.run_until([&] { return a.count(EV_MSG_FAILED) > 1; }, 60000);
  CHECK(a.find(EV_MSG_FAILED, 1)->e.tries == MAX_TRIES_SOS);
}

TEST(non_sos_is_busy_but_sos_replaces) {
  Net net;
  Node& a = net.add(1);
  net.add(2);
  CHECK(a.fsm.send_message(T_SI, 0xFF, net.now) == NodeFsm::SEND_OK);
  CHECK(a.fsm.send_message(T_NO, 0xFF, net.now) == NodeFsm::SEND_BUSY);
  CHECK(a.fsm.send_message(T_SOS, 0xFF, net.now) == NodeFsm::SEND_OK);
  CHECK(a.fsm.send_message(T_SOS, 0xFF, net.now) == NodeFsm::SEND_ALREADY);
  net.run_until([&] { return a.count(EV_MSG_ACKED) > 0; }, 5000);
  CHECK(a.find(EV_MSG_ACKED)->e.type == T_SOS);
}

TEST(foreign_traffic_ignored) {
  Net net;
  Node& b = net.add(2);
  uint8_t buf[MAX_PACKET];
  size_t n = build_message(buf, T_SOS, 1, 0xFF, 5);
  buf[0] = 0x34;  // another network's frame with the same PHY settings
  b.fsm.on_rx(buf, n, -80, 5, 0);
  CHECK(b.count(EV_MSG_RX) == 0 && b.fsm.stats().rx_foreign == 1);
  n = build_message(buf, T_SOS, 2, 0xFF, 5);  // my own id echoed back
  b.fsm.on_rx(buf, n, -80, 5, 0);
  CHECK(b.count(EV_MSG_RX) == 0);
  n = build_message(buf, T_SOS, 1, 7, 5);  // addressed to somebody else
  b.fsm.on_rx(buf, n, -80, 5, 0);
  CHECK(b.count(EV_MSG_RX) == 0);
}

// ---- voice ----------------------------------------------------------------------------------
static bool clip_equal(const ClipRx& c, const std::vector<uint8_t>& d) {
  return memcmp(c.data(), d.data(), d.size()) == 0;
}

TEST(clip_lossless) {
  Net net;
  Node& a = net.add(1);
  Node& b = net.add(2);
  auto clip = make_clip(125);
  uint32_t t0 = net.now;
  CHECK(a.fsm.send_clip(2, clip.data(), clip.size(), net.now) == NodeFsm::SEND_OK);
  net.run_until([&] { return a.count(EV_CLIP_TX_DONE) > 0 && b.fsm.role() == NodeFsm::Role::NONE; }, 10000);
  CHECK(a.find(EV_CLIP_TX_DONE)->e.code == CLIPTX_OK);
  const TimedEvent* done = b.find(EV_CLIP_RX_DONE);
  CHECK(done && done->e.code == CLIPRX_COMPLETE);
  CHECK(clip_equal(b.fsm.clip_rx(), clip));
  CHECK(net.count_tx(T_VFRAG) == 7 && net.count_tx(T_VANN) == 1);
  printf("  clip 10 s: send -> receiver has it: %u ms; sender notified: %u ms\n", done->t - t0,
         a.find(EV_CLIP_TX_DONE)->t - t0);
  CHECK(done->t - t0 < 2800);
  CHECK(a.fsm.profile() == PROFILE_ALARM && b.fsm.profile() == PROFILE_ALARM);
  CHECK(a.find(EV_CLIP_TX_DONE)->e.rssi_remote == net.rssi);
}

TEST(clip_one_fragment_lost_selective_retry) {
  Net net;
  Node& a = net.add(1);
  Node& b = net.add(2);
  auto clip = make_clip(125);
  int dropped = 0;
  net.drop = [&](const TxRec& r, int) {
    if ((r.buf[2] & 0x0F) == T_VFRAG && (r.buf[7] >> 4) == 2 && dropped == 0) {
      dropped++;
      return true;
    }
    return false;
  };
  uint32_t t0 = net.now;
  a.fsm.send_clip(2, clip.data(), clip.size(), net.now);
  net.run_until([&] { return a.count(EV_CLIP_TX_DONE) > 0 && b.fsm.role() == NodeFsm::Role::NONE; }, 15000);
  CHECK(a.find(EV_CLIP_TX_DONE)->e.code == CLIPTX_OK);
  CHECK(b.find(EV_CLIP_RX_DONE)->e.code == CLIPRX_COMPLETE && b.find(EV_CLIP_RX_DONE)->e.tries == 1);
  CHECK(clip_equal(b.fsm.clip_rx(), clip));
  CHECK(net.count_tx(T_VFRAG) == 8 && net.count_tx(T_NACK) == 1);  // only the missing one is resent
  printf("  1 fragment lost -> complete after %u ms\n", b.find(EV_CLIP_RX_DONE)->t - t0);
}

TEST(clip_last_fragment_lost) {
  Net net;
  Node& a = net.add(1);
  Node& b = net.add(2);
  auto clip = make_clip(125);
  int dropped = 0;
  net.drop = [&](const TxRec& r, int) {
    if ((r.buf[2] & 0x0F) == T_VFRAG && (r.buf[7] >> 4) == 6 && dropped++ == 0) return true;
    return false;
  };
  a.fsm.send_clip(2, clip.data(), clip.size(), net.now);
  net.run_until([&] { return a.count(EV_CLIP_TX_DONE) > 0; }, 15000);
  CHECK(a.find(EV_CLIP_TX_DONE)->e.code == CLIPTX_OK);
  CHECK(clip_equal(b.fsm.clip_rx(), clip));
}

TEST(clip_persistent_loss_plays_with_gap) {
  Net net;
  Node& a = net.add(1);
  Node& b = net.add(2);
  auto clip = make_clip(125);
  net.drop = [&](const TxRec& r, int) { return (r.buf[2] & 0x0F) == T_VFRAG && (r.buf[7] >> 4) == 3; };
  a.fsm.send_clip(2, clip.data(), clip.size(), net.now);
  net.run_until([&] { return a.count(EV_CLIP_TX_DONE) > 0 && b.fsm.role() == NodeFsm::Role::NONE; }, 20000);
  const TimedEvent* d = b.find(EV_CLIP_RX_DONE);
  CHECK(d && d->e.code == CLIPRX_PARTIAL && d->e.mask == (1u << 3));
  CHECK(a.find(EV_CLIP_TX_DONE)->e.code == CLIPTX_PARTIAL);
  CHECK(net.count_tx(T_NACK) == MAX_NACK_ROUNDS);
  CHECK(b.fsm.profile() == PROFILE_ALARM && a.fsm.profile() == PROFILE_ALARM);
}

TEST(clip_unannounced_no_response) {
  Net net;
  Node& a = net.add(1);
  Node& b = net.add(2);
  auto clip = make_clip(20);
  net.drop = [&](const TxRec& r, int) { return (r.buf[2] & 0x0F) == T_VANN; };
  a.fsm.send_clip(2, clip.data(), clip.size(), net.now);
  net.run_until([&] { return a.count(EV_CLIP_TX_DONE) > 0; }, 20000);
  CHECK(a.find(EV_CLIP_TX_DONE)->e.code == CLIPTX_NO_RESPONSE && a.find(EV_CLIP_TX_DONE)->e.tries == MAX_CLIP_ATTEMPTS);
  CHECK(b.count(EV_CLIP_RX_START) == 0 && b.fsm.profile() == PROFILE_ALARM);
}

TEST(clip_burst_out_of_range_receiver_recovers) {
  // ANN heard (SF12 reaches farther) but every voice fragment lost: receiver must return to the
  // alarm profile on its own, or it would stay deaf to SOS.
  Net net;
  Node& a = net.add(1);
  Node& b = net.add(2);
  auto clip = make_clip(40);
  net.drop = [&](const TxRec& r, int) { return (r.buf[2] & 0x0F) == T_VFRAG; };
  a.fsm.send_clip(2, clip.data(), clip.size(), net.now);
  net.run_until([&] { return b.count(EV_CLIP_RX_ABORT) > 0; }, 10000);
  CHECK(b.find(EV_CLIP_RX_ABORT)->e.code == CLIPRX_NO_SIGNAL);
  CHECK(b.fsm.profile() == PROFILE_ALARM);
}

TEST(clip_duplicate_announce_not_replayed) {
  Net net;
  Node& a = net.add(1);
  Node& b = net.add(2);
  auto clip = make_clip(30);
  int dropped = 0;
  net.drop = [&](const TxRec& r, int rx) {  // lose the final clip ACK once: the sender retries the whole clip
    if ((r.buf[2] & 0x0F) == T_ACK && r.buf[8] == T_VANN && dropped++ == 0) return true;
    (void)rx;
    return false;
  };
  a.fsm.send_clip(2, clip.data(), clip.size(), net.now);
  net.run_until([&] { return a.count(EV_CLIP_TX_DONE) > 0 && b.fsm.role() == NodeFsm::Role::NONE; }, 20000);
  CHECK(a.find(EV_CLIP_TX_DONE)->e.code == CLIPTX_OK && a.find(EV_CLIP_TX_DONE)->e.tries == 2);
  CHECK(b.count(EV_CLIP_RX_DONE) == 2);
  CHECK(b.find(EV_CLIP_RX_DONE, 0)->e.code == CLIPRX_COMPLETE && b.find(EV_CLIP_RX_DONE, 1)->e.code == CLIPRX_DUPLICATE);
}

// ---- SOS priority -----------------------------------------------------------------------------
TEST(sos_by_clip_sender_preempts_burst) {
  Net net;
  Node& a = net.add(1);
  Node& b = net.add(2);
  auto clip = make_clip(125);
  a.fsm.send_clip(2, clip.data(), clip.size(), net.now);
  net.run(1000);  // mid-burst
  CHECK(a.fsm.role() == NodeFsm::Role::VTX && b.fsm.role() == NodeFsm::Role::VRX);
  uint32_t t0 = net.now;
  CHECK(a.fsm.send_message(T_SOS, NODE_BROADCAST, net.now) == NodeFsm::SEND_OK);
  net.run_until([&] { return a.count(EV_MSG_ACKED) > 0; }, 8000);
  CHECK(a.find(EV_CLIP_TX_DONE) && a.find(EV_CLIP_TX_DONE)->e.code == CLIPTX_ABORTED_SOS);
  CHECK(b.find(EV_CLIP_RX_ABORT) && b.find(EV_CLIP_RX_ABORT)->e.code == CLIPRX_SENDER_ABORT);
  CHECK(b.count(EV_MSG_RX) == 1 && b.find(EV_MSG_RX)->e.type == T_SOS);
  printf("  SOS pressed mid-burst by the sender: delivered+ACKed in %u ms\n", a.find(EV_MSG_ACKED)->t - t0);
  CHECK(a.find(EV_MSG_ACKED)->t - t0 < 1500);
  CHECK(net.count_tx(T_VABORT) == 1);
  CHECK(a.fsm.profile() == PROFILE_ALARM && b.fsm.profile() == PROFILE_ALARM);
}

TEST(sos_by_clip_receiver_preempts_reception) {
  Net net;
  Node& a = net.add(1);
  Node& b = net.add(2);
  auto clip = make_clip(125);
  a.fsm.send_clip(2, clip.data(), clip.size(), net.now);
  net.run(900);
  uint32_t t0 = net.now;
  CHECK(b.fsm.send_message(T_SOS, NODE_BROADCAST, net.now) == NodeFsm::SEND_OK);
  net.run_until([&] { return b.count(EV_MSG_ACKED) > 0; }, 15000);
  uint32_t dt = b.find(EV_MSG_ACKED) ? b.find(EV_MSG_ACKED)->t - t0 : 99999;
  printf("  SOS pressed by the clip RECEIVER at ~0.9 s: delivered+ACKed in %u ms (sender is deaf until its burst ends)\n", dt);
  CHECK(a.count(EV_MSG_RX) == 1 && a.find(EV_MSG_RX)->e.type == T_SOS);
  CHECK(b.find(EV_CLIP_RX_ABORT) && b.find(EV_CLIP_RX_ABORT)->e.code == CLIPRX_LOCAL_SOS);
  CHECK(dt < 5000);  // worst case, documented in docs/protocolo.md sec. 6 (exceeds the 3 s idle target)
}

TEST(sos_by_bystander_during_clip) {
  Net net;
  Node& a = net.add(1);
  Node& b = net.add(2);
  Node& c = net.add(3);
  auto clip = make_clip(125);
  // C is out of the clip addressing: it ignores the ANN and stays in the alarm profile.
  a.fsm.send_clip(2, clip.data(), clip.size(), net.now);
  net.run(1000);
  uint32_t t0 = net.now;
  c.fsm.send_message(T_SOS, NODE_BROADCAST, net.now);
  net.run_until([&] { return c.count(EV_MSG_ACKED) > 0; }, 15000);
  uint32_t dt = c.find(EV_MSG_ACKED) ? c.find(EV_MSG_ACKED)->t - t0 : 99999;
  printf("  bystander SOS at ~1.0 s of a clip: ACKed in %u ms\n", dt);
  CHECK(c.count(EV_MSG_ACKED) == 1);
  CHECK(a.count(EV_MSG_RX) == 1 && b.count(EV_MSG_RX) == 1);
  CHECK(dt < 5000);
  // the clip itself still completes
  net.run_until([&] { return a.count(EV_CLIP_TX_DONE) > 0; }, 10000);
  CHECK(a.count(EV_CLIP_TX_DONE) == 1);
}

TEST(remote_sos_cancels_clip_before_burst) {
  Net net;
  Node& a = net.add(1);
  net.add(2);
  auto clip = make_clip(125);
  a.fsm.send_clip(9, clip.data(), clip.size(), net.now);
  net.run(300);  // ANN (248 ms) done, guard running: A is listening in the alarm profile
  uint8_t buf[MAX_PACKET];
  size_t n = build_message(buf, T_SOS, 2, NODE_BROADCAST, 7);
  a.fsm.on_rx(buf, n, -90, 5, net.now);
  net.run(600);
  CHECK(a.find(EV_CLIP_TX_DONE) && a.find(EV_CLIP_TX_DONE)->e.code == CLIPTX_REMOTE_SOS);
  CHECK(a.count(EV_MSG_RX) == 1 && net.count_tx(T_VABORT) == 1 && net.count_tx(T_ACK) == 1);
  CHECK(a.fsm.role() == NodeFsm::Role::NONE && a.fsm.profile() == PROFILE_ALARM);
}

TEST(radio_failure_is_reported) {
  Net net;
  Node& a = net.add(1);
  net.add(2);
  a.radio_fail = true;
  a.fsm.send_message(T_PRUEBA, 2, net.now);
  net.run(2000);
  CHECK(a.count(EV_RADIO_ERROR) >= 1);
  CHECK(a.count(EV_MSG_FAILED) == 1);
  auto clip = make_clip(10);
  a.fsm.send_clip(2, clip.data(), clip.size(), net.now);
  net.run(500);
  CHECK(a.find(EV_CLIP_TX_DONE) && a.find(EV_CLIP_TX_DONE)->e.code == CLIPTX_RADIO_ERROR);
  CHECK(a.fsm.role() == NodeFsm::Role::NONE);
}

TEST(broadcast_ack_slots_do_not_collide) {
  Net net;
  Node& a = net.add(1);
  Node& b = net.add(2);
  Node& c = net.add(3);
  net.add(4);
  a.fsm.send_message(T_PELIGRO, NODE_BROADCAST, net.now);
  net.run_until([&] { return a.count(EV_ACK_EXTRA) >= 2; }, 5000);
  CHECK(a.count(EV_MSG_ACKED) == 1 && a.count(EV_ACK_EXTRA) == 2);  // 3 receivers, 3 distinct slots
  CHECK(a.fsm.message_pending() == false);
  (void)b;
  (void)c;
}

int main() {
  RUN(codec_roundtrip_and_rejects);
  RUN(airtime_matches_scientist);
  RUN(pair_pack_roundtrip);
  RUN(clip_rx_reassembly);
  RUN(counters_and_wraparound);
  RUN(message_ack_cycle_time);
  RUN(prueba_reports_rssi_snr_and_is_silent);
  RUN(ack_lost_retry_no_double_alarm);
  RUN(message_gives_up_without_peer);
  RUN(non_sos_is_busy_but_sos_replaces);
  RUN(foreign_traffic_ignored);
  RUN(clip_lossless);
  RUN(clip_one_fragment_lost_selective_retry);
  RUN(clip_last_fragment_lost);
  RUN(clip_persistent_loss_plays_with_gap);
  RUN(clip_unannounced_no_response);
  RUN(clip_burst_out_of_range_receiver_recovers);
  RUN(clip_duplicate_announce_not_replayed);
  RUN(sos_by_clip_sender_preempts_burst);
  RUN(sos_by_clip_receiver_preempts_reception);
  RUN(sos_by_bystander_during_clip);
  RUN(remote_sos_cancels_clip_before_burst);
  RUN(radio_failure_is_reported);
  RUN(broadcast_ack_slots_do_not_collide);
  printf("\n%d checks passed, %d failed\n", g_pass, g_fail);
  return g_fail ? 1 : 0;
}
