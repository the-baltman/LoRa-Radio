// Node protocol state machine: messages + ACK, voice clips (announce / burst / selective retry),
// SOS priority. Portable (no Arduino): the radio and the UI are reached through `Host`, time is
// passed in, so the same code runs on the ESP32 and in the host tests (tests/host).
//
// Radio contract assumed by this class:
//  * the radio is in continuous RX whenever it is not transmitting (the host driver re-arms RX
//    BEFORE calling on_tx_done());
//  * radio_tx() only starts the transmission; the host calls on_tx_done() when it ends;
//  * on_rx() is called once per CRC-valid packet, at the end of its reception.
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <stdint.h>

#include "airtime.h"
#include "clip_rx.h"
#include "protocol.h"
#include "recent.h"

namespace lr {

// ---- Timing and retry policy ([ms]; derivations in docs/protocolo.md sec. 7) -----------------
constexpr uint32_t ACK_TURNAROUND_MS = 20;      // RX->TX turn-around allowance of the ACKer
constexpr uint8_t ACK_SLOTS = 4;                // broadcast: ACK slot = (node_id - 1) % 4
constexpr uint32_t ACK_SLOT_GUARD_MS = 20;
constexpr uint32_t ACK_MARGIN_MS = 200;         // scientist: timeout = ToA(ACK) + 200 ms
constexpr uint8_t MAX_TRIES_SOS = 10;           // keep trying: it is the alarm
constexpr uint8_t MAX_TRIES_DEFAULT = 3;
constexpr uint8_t MAX_TRIES_PRUEBA = 1;         // PRUEBA measures the link: a retry would hide the loss
constexpr uint32_t VOICE_GUARD_MS = 150;        // ANN end -> burst start: lets receivers switch SF
constexpr uint32_t FRAG_GAP_MS = 10;            // between fragments of a burst
constexpr uint32_t RESP_WAIT_MS = 400;          // sender listens for ACK/NACK after a burst
constexpr uint32_t RETX_START_MS = 30;          // NACK received -> first retransmitted fragment
constexpr uint32_t FRAG_IDLE_MS = 150;          // receiver: silence that means "burst over"
constexpr uint32_t NACK_MARGIN_MS = 60;         // receiver: wait this past the estimated burst end
constexpr uint32_t RETX_WAIT_MS = 700;          // receiver: no fragment after a NACK -> give up
constexpr uint32_t FIRST_FRAG_SLACK_MS = 500;   // receiver: slack for a late burst start
constexpr uint32_t VR_CLOSE_FALLBACK_MS = 500;  // receiver: leave voice profile even if the ACK fails
constexpr uint32_t RETRY_LISTEN_MS = 1800;      // sender: alarm-profile listening gap before re-announcing.
                                                // Must be >= SOS retry period (broadcast: 1272 ms ACK wait + 248 ms ToA = 1519 ms)
                                                // + ToA of the SOS (248 ms); tests/host checks the inequality
constexpr uint8_t MAX_NACK_ROUNDS = 2;          // selective retry rounds, then play with a gap
constexpr uint8_t MAX_CLIP_ATTEMPTS = 2;        // whole-clip attempts when nobody answers
constexpr uint16_t MAX_CLIP_PAIRS = 100;        // 8 s of audio (100 x 80 ms): arbitration 2026-10-02, SOS < 3 s

enum EventKind : uint8_t {
  EV_MSG_RX,        // new message for me (not a duplicate): src, type, msg_id, rssi, snr
  EV_MSG_ACKED,     // my message was ACKed: type, msg_id, src = acker, tries, rssi/snr (of the ACK),
                    //   rssi_remote/snr_remote (as measured by the acker on my message)
  EV_MSG_FAILED,    // no ACK after all tries (code 0), or replaced by an SOS (code 1)
  EV_ACK_EXTRA,     // another node ACKed a broadcast already completed: count = ACKs so far
  EV_TX,            // a packet went on air: type, dst, msg_id, count = payload bytes
  EV_CLIP_TX_DONE,  // code = ClipTxResult, tries = attempts, mask = fragments never delivered
  EV_CLIP_RX_START, // src, msg_id, count = fragments announced
  EV_CLIP_RX_DONE,  // clip ready in clip_rx(): code = CLIPRX_COMPLETE|CLIPRX_PARTIAL|CLIPRX_DUPLICATE
  EV_CLIP_RX_ABORT, // code = ClipRxEnd reason
  EV_RADIO_ERROR,   // radio_tx() failed
};

enum ClipTxResult : uint8_t {
  CLIPTX_OK = 0,
  CLIPTX_PARTIAL,      // receiver gave up after MAX_NACK_ROUNDS and plays with a gap
  CLIPTX_NO_RESPONSE,  // nobody answered after MAX_CLIP_ATTEMPTS
  CLIPTX_INCOMPLETE,   // still NACKing after MAX_NACK_ROUNDS
  CLIPTX_ABORTED_SOS,  // I pressed SOS during my clip
  CLIPTX_REMOTE_SOS,   // someone else's SOS: before the burst, or the receiver's ACK_SOS in its voice slot
  CLIPTX_CANCELLED,    // user cancel
  CLIPTX_RADIO_ERROR,
};

enum ClipRxEnd : uint8_t {
  CLIPRX_COMPLETE = 0,
  CLIPRX_PARTIAL,       // gaps remain: play with silence
  CLIPRX_DUPLICATE,     // clip already played (sender retried because my ACK was lost)
  CLIPRX_NO_SIGNAL,     // announced but the burst never came
  CLIPRX_SENDER_ABORT,  // VABORT received
  CLIPRX_LOCAL_SOS,     // I pressed SOS
  CLIPRX_GAVE_UP,       // retransmissions never arrived
};

struct Event {
  EventKind kind = EV_TX;
  uint8_t src = 0, dst = 0, type = 0;
  uint16_t msg_id = 0;
  uint8_t code = 0, tries = 0, count = 0;
  uint16_t mask = 0;
  float rssi = 0, snr = 0;
  float rssi_remote = 0, snr_remote = 0;
};

class Host {
 public:
  virtual ~Host() {}
  // Re-tune the modem to `p` and (re)start continuous RX. Called only when the profile changes.
  virtual void radio_set_profile(ProfileId p) = 0;
  // Start transmitting in the current profile. false = the radio refused (SPI error, busy).
  virtual bool radio_tx(const uint8_t* buf, size_t len) = 0;
  virtual void notify(const Event& e) = 0;
};

class NodeFsm {
 public:
  enum class Role : uint8_t { NONE, VTX, VRX };
  enum SendStatus : uint8_t { SEND_OK, SEND_BUSY, SEND_BAD_ARG, SEND_ALREADY };

  struct Stats {
    uint32_t rx_ok = 0, rx_bad = 0, rx_foreign = 0, rx_dup = 0, tx = 0, tx_fail = 0;
  };
  // Per-source counters from msg_id gaps (message types only; clips also consume ids, so `lost`
  // is an upper-bound estimate in mixed traffic and exact in a PRUEBA-only range test).
  struct PeerStat {
    bool used = false;
    uint8_t src = 0;
    uint16_t last_id = 0;
    uint32_t rx = 0, lost = 0;
  };
  static constexpr int MAX_PEERS = 4;

  NodeFsm(Host& host, uint8_t node_id, uint16_t first_msg_id);

  void begin(uint32_t now);
  void on_rx(const uint8_t* buf, size_t len, float rssi, float snr, uint32_t now);
  void on_tx_done(uint32_t now);
  void tick(uint32_t now);

  // SOS preempts everything (clip in progress, pending message). Others return SEND_BUSY while
  // a message or clip is in flight.
  SendStatus send_message(uint8_t type, uint8_t dst, uint32_t now);
  // `data`: whole pairs of 13 B (see audio/pair_pack.h), must stay valid until EV_CLIP_TX_DONE.
  SendStatus send_clip(uint8_t dst, const uint8_t* data, uint16_t bytes, uint32_t now);
  void cancel_clip(uint32_t now);

  Role role() const { return role_; }
  bool tx_busy() const { return tx_kind_ != TXK_NONE; }
  bool message_pending() const { return pm_.active; }
  ProfileId profile() const { return cur_profile_; }
  const ClipRx& clip_rx() const { return clip_; }
  const Stats& stats() const { return stats_; }
  const PeerStat* peer(uint8_t src) const;
  uint8_t id() const { return id_; }
  // Milliseconds until tick() has something to do (0 = now, UINT32_MAX = nothing scheduled).
  // The firmware uses it to size the light-sleep; DIO1 wakes it earlier for any packet.
  uint32_t ms_until_work(uint32_t now) const;
  // True when the node is idle in the alarm profile and a light-sleep is safe.
  bool can_sleep() const { return role_ == Role::NONE && tx_kind_ == TXK_NONE; }

 private:
  enum TxKind : uint8_t { TXK_NONE, TXK_ACK, TXK_NACK, TXK_MSG, TXK_ANN, TXK_FRAG, TXK_ABORT };
  enum ActKind : uint8_t { ACT_NONE, ACT_MSG, ACT_ANN, ACT_FRAG, ACT_ABORT };
  enum VtSub : uint8_t { VT_ANN, VT_GUARD, VT_BURST, VT_WAIT, VT_ABORTING, VT_COOLDOWN };

  struct Act {
    ActKind kind = ACT_NONE;
    uint32_t due = 0;
  };
  struct QueuedAck {
    bool used = false;
    TxKind kind = TXK_ACK;
    ProfileId profile = PROFILE_ALARM;
    uint32_t due = 0;
    uint8_t len = 0;
    uint8_t buf[12];
  };
  static constexpr int ACK_Q = 3;

  struct Pm {  // one outgoing message awaiting its ACK
    bool active = false, waiting = false;
    uint8_t type = 0, dst = 0, tries = 0, max_tries = 0, acks = 0;
    uint16_t msg_id = 0;
    uint32_t deadline = 0;
  };
  struct Vt {  // clip being sent
    uint8_t dst = 0, frag_cnt = 0, attempt = 0, round = 0, cur_frag = 0;
    uint16_t msg_id = 0, pairs = 0, sendmask = 0;
    const uint8_t* data = nullptr;
    VtSub sub = VT_ANN;
    uint32_t deadline = 0;
    bool got_resp = false, abort_req = false, announced = false;
    ClipTxResult abort_reason = CLIPTX_CANCELLED;
  };
  struct Vr {  // clip being received
    uint8_t src = 0, rounds = 0, frags_since_nack = 0;
    bool got_any = false, nack_queued = false, closing = false, duplicate = false, dst_bcast = false, sos_pending = false;
    uint32_t first_deadline = 0, last_rx = 0, burst_end_est = 0, nack_done = 0, close_deadline = 0;
    float rssi = 0, snr = 0;  // of the last fragment, echoed in the final ACK
  };

  // --- rx handlers
  void rx_message(const Packet& p, float rssi, float snr, uint32_t now);
  void rx_ack(const Packet& p, float rssi, float snr, uint32_t now);
  void rx_nack(const Packet& p, uint32_t now);
  void rx_vann(const Packet& p, uint32_t now);
  void rx_vfrag(const Packet& p, uint32_t now);
  void rx_vabort(const Packet& p, uint32_t now);
  void note_peer(uint8_t src, uint16_t id);

  // --- tx plumbing
  void service_tx(uint32_t now);
  bool start_tx(TxKind kind, ProfileId profile, const uint8_t* buf, size_t len, uint32_t now);
  void queue_ack(TxKind kind, ProfileId profile, uint32_t due, const uint8_t* buf, size_t len);
  void drop_acks_not(ProfileId keep);
  ProfileId desired_profile() const;
  void apply_profile();
  uint32_t ack_delay_ms(ProfileId profile, bool dst_was_bcast) const;
  uint32_t ack_wait_ms(uint8_t dst) const;
  void emit(const Event& e) { host_.notify(e); }

  // --- messages
  void pm_complete(const Packet& p, const AckInfo& a, float rssi, float snr, uint32_t now);
  void pm_fail(uint8_t code);

  // --- voice tx
  void vt_start_attempt(uint32_t now);
  void vt_request_abort(ClipTxResult reason, uint32_t now);
  void vt_do_abort(uint32_t now);
  void vt_finish(ClipTxResult result, uint32_t now);
  void vt_retransmit(uint16_t mask, uint32_t now);
  static uint8_t lowest_bit(uint16_t m);

  // --- voice rx
  uint32_t burst_time_ms(uint16_t mask) const;
  void vr_complete(uint32_t now);
  void vr_finish_partial(bool send_ack, uint32_t now);
  void vr_abort(ClipRxEnd why, uint32_t now);
  void vr_close(uint32_t now);
  void vr_queue_reply(TxKind kind, uint8_t status, uint16_t mask, uint32_t now);
  void vr_tick(uint32_t now);
  void vt_tick(uint32_t now);

  Host& host_;
  const uint8_t id_;
  uint16_t next_id_;
  Role role_ = Role::NONE;
  ProfileId cur_profile_ = PROFILE_ALARM;
  TxKind tx_kind_ = TXK_NONE;
  Act main_;
  QueuedAck ackq_[ACK_Q];
  Pm pm_;
  Vt vt_;
  Vr vr_;
  ClipRx clip_;
  RecentTable recent_;
  PeerStat peers_[MAX_PEERS];
  Stats stats_;
  uint16_t acked_id_ = 0;
  uint32_t acked_until_ = 0;
  uint8_t acked_count_ = 0;
  uint8_t acked_type_ = 0;
  bool msg_tx_stale_ = false;  // the MSG on air belongs to a pm_ that an SOS has already replaced
  uint8_t last_vann_src_ = 0;
  uint16_t last_vann_id_ = 0;
  bool last_vann_valid_ = false;
};

}  // namespace lr
