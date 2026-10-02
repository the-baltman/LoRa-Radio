// SPDX-License-Identifier: GPL-3.0-or-later
#include "node_fsm.h"

#include <string.h>

namespace lr {

namespace {
inline bool reached(uint32_t now, uint32_t t) { return static_cast<int32_t>(now - t) >= 0; }
constexpr uint32_t NEVER = 0xFFFFFFFFu;
}  // namespace

NodeFsm::NodeFsm(Host& host, uint8_t node_id, uint16_t first_msg_id)
    : host_(host), id_(node_id), next_id_(first_msg_id) {}

void NodeFsm::begin(uint32_t) {
  cur_profile_ = PROFILE_ALARM;
  host_.radio_set_profile(PROFILE_ALARM);
}

const NodeFsm::PeerStat* NodeFsm::peer(uint8_t src) const {
  for (int i = 0; i < MAX_PEERS; i++)
    if (peers_[i].used && peers_[i].src == src) return &peers_[i];
  return nullptr;
}

void NodeFsm::note_peer(uint8_t src, uint16_t id) {
  PeerStat* free_slot = nullptr;
  for (int i = 0; i < MAX_PEERS; i++) {
    PeerStat& p = peers_[i];
    if (p.used && p.src == src) {
      int32_t gap = seq_gap(p.last_id, id);
      if (gap >= 0) {
        p.lost += static_cast<uint32_t>(gap);
        p.last_id = id;
        p.rx++;
      }
      return;
    }
    if (!p.used && !free_slot) free_slot = &p;
  }
  if (!free_slot) free_slot = &peers_[0];  // more peers than slots: recycle the first
  *free_slot = PeerStat{true, src, id, 1, 0};
}

// ---- profile / tx plumbing ----------------------------------------------------------------

ProfileId NodeFsm::desired_profile() const {
  if (role_ == Role::VRX) return PROFILE_VOICE_C2_1300;
  if (role_ == Role::VTX && (vt_.sub == VT_BURST || vt_.sub == VT_WAIT || vt_.sub == VT_ABORTING))
    return PROFILE_VOICE_C2_1300;
  return PROFILE_ALARM;
}

void NodeFsm::apply_profile() {
  if (tx_kind_ != TXK_NONE) return;  // re-applied from on_tx_done()
  ProfileId d = desired_profile();
  if (d != cur_profile_) {
    host_.radio_set_profile(d);
    cur_profile_ = d;
  }
}

uint32_t NodeFsm::ack_delay_ms(ProfileId profile, bool bcast) const {
  uint32_t d = ACK_TURNAROUND_MS;
  if (bcast) {
    uint32_t slot = (id_ - 1u) % ACK_SLOTS;
    d += slot * (toa_ms(profile, HEADER_LEN + 4) + ACK_SLOT_GUARD_MS);
  }
  return d;
}

uint32_t NodeFsm::ack_wait_ms(uint8_t dst) const {
  uint32_t toa = toa_ms(PROFILE_ALARM, HEADER_LEN + 4);
  uint32_t w = ACK_TURNAROUND_MS + toa + ACK_MARGIN_MS;
  if (dst == NODE_BROADCAST) w += (ACK_SLOTS - 1u) * (toa + ACK_SLOT_GUARD_MS);
  return w;
}

void NodeFsm::queue_ack(TxKind kind, ProfileId profile, uint32_t due, const uint8_t* buf, size_t len) {
  for (int i = 0; i < ACK_Q; i++) {
    if (!ackq_[i].used) {
      ackq_[i].used = true;
      ackq_[i].kind = kind;
      ackq_[i].profile = profile;
      ackq_[i].due = due;
      ackq_[i].len = static_cast<uint8_t>(len);
      memcpy(ackq_[i].buf, buf, len);
      return;
    }
  }
  // queue full: the peer retries, dropping is safe
}

void NodeFsm::drop_acks_not(ProfileId keep) {
  for (int i = 0; i < ACK_Q; i++)
    if (ackq_[i].used && ackq_[i].profile != keep) ackq_[i].used = false;
}

bool NodeFsm::start_tx(TxKind kind, ProfileId profile, const uint8_t* buf, size_t len, uint32_t) {
  if (cur_profile_ != profile) {
    host_.radio_set_profile(profile);
    cur_profile_ = profile;
  }
  if (!host_.radio_tx(buf, len)) {
    stats_.tx_fail++;
    Event e;
    e.kind = EV_RADIO_ERROR;
    emit(e);
    apply_profile();
    return false;
  }
  tx_kind_ = kind;
  stats_.tx++;
  Event e;
  e.kind = EV_TX;
  e.type = buf[2] & 0x0F;
  e.dst = buf[4];
  e.msg_id = static_cast<uint16_t>(buf[5] | (buf[6] << 8));
  e.count = static_cast<uint8_t>(len);
  emit(e);
  return true;
}

void NodeFsm::service_tx(uint32_t now) {
  if (tx_kind_ != TXK_NONE) return;

  int best = -1;
  for (int i = 0; i < ACK_Q; i++)
    if (ackq_[i].used && reached(now, ackq_[i].due) &&
        (best < 0 || static_cast<int32_t>(ackq_[i].due - ackq_[best].due) < 0))
      best = i;
  if (best >= 0) {
    QueuedAck q = ackq_[best];
    ackq_[best].used = false;
    if (!start_tx(q.kind, q.profile, q.buf, q.len, now) && q.kind == TXK_NACK) vr_.nack_queued = false;
    return;
  }

  if (main_.kind == ACT_NONE || !reached(now, main_.due)) return;
  uint8_t buf[MAX_PACKET];
  size_t len = 0;
  ProfileId prof = PROFILE_ALARM;
  TxKind k = TXK_NONE;
  const ActKind act = main_.kind;
  switch (act) {
    case ACT_MSG:
      len = build_message(buf, pm_.type, id_, pm_.dst, pm_.msg_id);
      k = TXK_MSG;
      break;
    case ACT_ANN:
      len = build_vann(buf, id_, vt_.dst, vt_.msg_id, PROFILE_VOICE_C2_1300, vt_.frag_cnt, vt_.pairs);
      k = TXK_ANN;
      break;
    case ACT_FRAG: {
      uint8_t pairs = static_cast<uint8_t>(frag_pairs(vt_.pairs, vt_.cur_frag));
      len = build_vfrag(buf, id_, vt_.dst, vt_.msg_id, vt_.cur_frag, vt_.frag_cnt,
                        vt_.data + static_cast<size_t>(vt_.cur_frag) * FRAG_AUDIO_BYTES, pairs);
      prof = PROFILE_VOICE_C2_1300;
      k = TXK_FRAG;
      break;
    }
    case ACT_ABORT:
      len = build_vabort(buf, id_, vt_.dst, vt_.msg_id);
      prof = PROFILE_VOICE_C2_1300;
      k = TXK_ABORT;
      break;
    default:
      return;
  }
  main_.kind = ACT_NONE;
  if (len == 0) return;  // unreachable with validated arguments
  if (start_tx(k, prof, buf, len, now)) return;

  // radio refused
  if (act == ACT_MSG) {
    pm_.tries++;
    pm_.waiting = true;
    pm_.deadline = now + 100;
  } else {
    vt_finish(CLIPTX_RADIO_ERROR, now);
  }
}

// ---- commands -----------------------------------------------------------------------------

NodeFsm::SendStatus NodeFsm::send_message(uint8_t type, uint8_t dst, uint32_t now) {
  if (!is_message_type(type) || dst == NODE_INVALID) return SEND_BAD_ARG;
  const bool sos = (type == T_SOS);
  if (sos) {
    if (pm_.active && pm_.type == T_SOS) return SEND_ALREADY;
  } else if (role_ != Role::NONE || pm_.active) {
    return SEND_BUSY;
  }

  if (sos && pm_.active) {  // replace a lower-priority message in flight
    if (tx_kind_ == TXK_MSG) msg_tx_stale_ = true;
    pm_fail(1);
    if (main_.kind == ACT_MSG) main_.kind = ACT_NONE;
  }
  if (sos && role_ == Role::VRX) vr_abort(CLIPRX_LOCAL_SOS, now);

  pm_ = Pm();
  pm_.active = true;
  pm_.type = type;
  pm_.dst = dst;
  pm_.msg_id = next_id_++;
  pm_.max_tries = sos ? MAX_TRIES_SOS : (type == T_PRUEBA ? MAX_TRIES_PRUEBA : MAX_TRIES_DEFAULT);

  if (sos && role_ == Role::VTX) {
    vt_request_abort(CLIPTX_ABORTED_SOS, now);  // vt_finish() kicks the SOS out
  } else {
    main_.kind = ACT_MSG;
    main_.due = now;
  }
  service_tx(now);
  return SEND_OK;
}

NodeFsm::SendStatus NodeFsm::send_clip(uint8_t dst, const uint8_t* data, uint16_t bytes, uint32_t now) {
  if (data == nullptr || dst == NODE_INVALID || bytes == 0 || bytes % PAIR_BYTES != 0)
    return SEND_BAD_ARG;
  uint16_t pairs = static_cast<uint16_t>(bytes / PAIR_BYTES);
  if (pairs > MAX_CLIP_PAIRS) return SEND_BAD_ARG;
  if (role_ != Role::NONE || pm_.active) return SEND_BUSY;

  vt_ = Vt();
  vt_.dst = dst;
  vt_.data = data;
  vt_.pairs = pairs;
  vt_.frag_cnt = frag_count_for(pairs);
  vt_.msg_id = next_id_++;
  role_ = Role::VTX;
  vt_start_attempt(now);
  service_tx(now);
  return SEND_OK;
}

void NodeFsm::cancel_clip(uint32_t now) {
  if (role_ == Role::VTX) vt_request_abort(CLIPTX_CANCELLED, now);
}

// ---- rx -----------------------------------------------------------------------------------

void NodeFsm::on_rx(const uint8_t* buf, size_t len, float rssi, float snr, uint32_t now) {
  Packet p;
  ParseStatus st = parse_packet(buf, len, &p);
  if (st != PARSE_OK) {
    if (st == PARSE_BAD_NET) stats_.rx_foreign++;
    else stats_.rx_bad++;
    return;
  }
  if (p.h.src == id_) return;
  if (p.h.dst != id_ && p.h.dst != NODE_BROADCAST) return;
  stats_.rx_ok++;
  switch (p.h.type) {
    case T_SOS: case T_PELIGRO: case T_PRUEBA: case T_SI: case T_NO:
      rx_message(p, rssi, snr, now);
      break;
    case T_ACK: rx_ack(p, rssi, snr, now); break;
    case T_NACK: rx_nack(p, now); break;
    case T_VANN: rx_vann(p, now); break;
    case T_VFRAG:
      vr_.rssi = rssi;
      vr_.snr = snr;
      rx_vfrag(p, now);
      break;
    case T_VABORT: rx_vabort(p, now); break;
    default: break;
  }
  service_tx(now);
}

void NodeFsm::rx_message(const Packet& p, float rssi, float snr, uint32_t now) {
  if (role_ == Role::VRX) return;  // cannot happen physically (voice profile); defensive
  bool dup = recent_.seen_and_record(p.h.src, p.h.msg_id, p.h.type, now);
  if (dup) {
    stats_.rx_dup++;
  } else {
    note_peer(p.h.src, p.h.msg_id);
    Event e;
    e.kind = EV_MSG_RX;
    e.src = p.h.src;
    e.dst = p.h.dst;
    e.type = p.h.type;
    e.msg_id = p.h.msg_id;
    e.rssi = rssi;
    e.snr = snr;
    emit(e);
    if (p.h.type == T_SOS && role_ == Role::VTX) vt_request_abort(CLIPTX_REMOTE_SOS, now);
  }
  // Always ACK, also duplicates: the sender retried because it never heard our previous ACK.
  uint8_t buf[MAX_PACKET];
  size_t n = build_ack(buf, id_, p.h.src, p.h.msg_id, p.h.type, ACK_OK, rssi, snr);
  queue_ack(TXK_ACK, PROFILE_ALARM, now + ack_delay_ms(PROFILE_ALARM, p.h.dst == NODE_BROADCAST), buf, n);
}

void NodeFsm::rx_ack(const Packet& p, float rssi, float snr, uint32_t now) {
  AckInfo a;
  if (!parse_ack(p, &a)) return;
  if (role_ == Role::VTX && vt_.sub == VT_WAIT && p.h.msg_id == vt_.msg_id && a.acked_type == T_VANN) {
    vt_.got_resp = true;
    Event e;
    e.kind = EV_CLIP_TX_DONE;
    e.rssi = rssi;
    e.snr = snr;
    e.rssi_remote = a.rssi_dbm;
    e.snr_remote = a.snr_db;
    e.src = p.h.src;
    e.msg_id = vt_.msg_id;
    e.tries = vt_.attempt;
    e.code = (a.status == ACK_PARTIAL) ? CLIPTX_PARTIAL : CLIPTX_OK;
    role_ = Role::NONE;
    main_.kind = ACT_NONE;
    vt_ = Vt();
    emit(e);
    apply_profile();
    return;
  }
  if (pm_.active && pm_.msg_id == p.h.msg_id && pm_.type == a.acked_type) {
    pm_complete(p, a, rssi, snr, now);
    return;
  }
  if (acked_count_ > 0 && !reached(now, acked_until_) && p.h.msg_id == acked_id_ && a.acked_type == acked_type_) {
    acked_count_++;
    Event e;
    e.kind = EV_ACK_EXTRA;
    e.src = p.h.src;
    e.type = acked_type_;
    e.msg_id = acked_id_;
    e.count = acked_count_;
    e.rssi = rssi;
    e.snr = snr;
    emit(e);
  }
}

void NodeFsm::rx_nack(const Packet& p, uint32_t now) {
  uint16_t mask;
  if (!parse_nack(p, &mask)) return;
  if (role_ != Role::VTX || vt_.sub != VT_WAIT || p.h.msg_id != vt_.msg_id) return;
  mask &= static_cast<uint16_t>((1u << vt_.frag_cnt) - 1u);
  if (mask == 0) return;
  vt_.got_resp = true;
  vt_.round++;
  if (vt_.round > MAX_NACK_ROUNDS) {
    vt_.sendmask = mask;
    vt_finish(CLIPTX_INCOMPLETE, now);
    return;
  }
  vt_retransmit(mask, now);
}

void NodeFsm::rx_vann(const Packet& p, uint32_t now) {
  if (role_ != Role::NONE || pm_.active) return;
  VannInfo v;
  if (!parse_vann(p, &v) || v.profile != PROFILE_VOICE_C2_1300) return;
  if (!clip_.begin(p.h.src, p.h.msg_id, v.frag_cnt, v.pairs)) return;
  role_ = Role::VRX;
  vr_ = Vr();
  vr_.src = p.h.src;
  vr_.dst_bcast = (p.h.dst == NODE_BROADCAST);
  vr_.duplicate = last_vann_valid_ && last_vann_src_ == p.h.src && last_vann_id_ == p.h.msg_id;
  uint32_t t0 = now + VOICE_GUARD_MS;
  vr_.burst_end_est = t0 + burst_time_ms(clip_.all_mask());
  vr_.first_deadline = t0 + toa_ms(PROFILE_VOICE_C2_1300, HEADER_LEN + frag_pairs(v.pairs, 0) * PAIR_BYTES) +
                       FIRST_FRAG_SLACK_MS;
  vr_.last_rx = now;
  drop_acks_not(PROFILE_VOICE_C2_1300);
  apply_profile();
  Event e;
  e.kind = EV_CLIP_RX_START;
  e.src = p.h.src;
  e.msg_id = p.h.msg_id;
  e.count = v.frag_cnt;
  emit(e);
}

void NodeFsm::rx_vfrag(const Packet& p, uint32_t now) {
  if (role_ != Role::VRX || vr_.closing || p.h.src != vr_.src || p.h.msg_id != clip_.msg_id()) return;
  if (p.h.frag_cnt != clip_.frag_cnt()) return;
  ClipRx::AddResult r = clip_.add(p.h.frag_idx, p.payload, static_cast<uint8_t>(p.plen / PAIR_BYTES));
  if (r == ClipRx::ADD_REJECT) return;
  vr_.got_any = true;
  vr_.last_rx = now;
  if (r == ClipRx::ADD_NEW) vr_.frags_since_nack++;
  if (clip_.complete()) vr_complete(now);
}

void NodeFsm::rx_vabort(const Packet& p, uint32_t now) {
  if (role_ == Role::VRX && p.h.src == vr_.src && p.h.msg_id == clip_.msg_id()) vr_abort(CLIPRX_SENDER_ABORT, now);
}

// ---- messages -----------------------------------------------------------------------------

void NodeFsm::pm_complete(const Packet& p, const AckInfo& a, float rssi, float snr, uint32_t now) {
  Event e;
  e.kind = EV_MSG_ACKED;
  e.type = pm_.type;
  e.msg_id = pm_.msg_id;
  e.src = p.h.src;
  e.tries = pm_.tries;
  e.rssi = rssi;
  e.snr = snr;
  e.rssi_remote = a.rssi_dbm;
  e.snr_remote = a.snr_db;
  acked_id_ = pm_.msg_id;
  acked_type_ = pm_.type;
  acked_count_ = 1;
  acked_until_ = now + ack_wait_ms(pm_.dst);
  if (main_.kind == ACT_MSG) main_.kind = ACT_NONE;
  pm_ = Pm();
  emit(e);
}

void NodeFsm::pm_fail(uint8_t code) {
  Event e;
  e.kind = EV_MSG_FAILED;
  e.type = pm_.type;
  e.msg_id = pm_.msg_id;
  e.tries = pm_.tries;
  e.code = code;
  pm_ = Pm();
  emit(e);
}

// ---- voice tx -----------------------------------------------------------------------------

uint8_t NodeFsm::lowest_bit(uint16_t m) {
  uint8_t i = 0;
  while (i < 16 && !(m & (1u << i))) i++;
  return i;
}

void NodeFsm::vt_start_attempt(uint32_t now) {
  vt_.attempt++;
  vt_.round = 0;
  vt_.sendmask = static_cast<uint16_t>((1u << vt_.frag_cnt) - 1u);
  vt_.got_resp = false;
  vt_.announced = false;
  vt_.sub = VT_ANN;
  main_.kind = ACT_ANN;
  main_.due = now;
}

void NodeFsm::vt_retransmit(uint16_t mask, uint32_t now) {
  vt_.sendmask = mask;
  vt_.sub = VT_BURST;
  vt_.cur_frag = lowest_bit(mask);
  main_.kind = ACT_FRAG;
  main_.due = now + RETX_START_MS;
}

void NodeFsm::vt_request_abort(ClipTxResult reason, uint32_t now) {
  vt_.abort_req = true;
  vt_.abort_reason = reason;
  if (tx_kind_ == TXK_ANN || tx_kind_ == TXK_FRAG) return;  // finishes in on_tx_done()
  vt_do_abort(now);
}

void NodeFsm::vt_do_abort(uint32_t now) {
  if (vt_.announced) {
    // Receivers already left the alarm profile: tell them, or they sit deaf for ~1 s.
    vt_.sub = VT_ABORTING;
    main_.kind = ACT_ABORT;
    main_.due = now;
  } else {
    vt_finish(vt_.abort_reason, now);
  }
}

void NodeFsm::vt_finish(ClipTxResult result, uint32_t now) {
  Event e;
  e.kind = EV_CLIP_TX_DONE;
  e.code = result;
  e.tries = vt_.attempt;
  e.msg_id = vt_.msg_id;
  e.mask = vt_.sendmask;
  role_ = Role::NONE;
  main_.kind = ACT_NONE;
  vt_ = Vt();
  emit(e);
  if (pm_.active && !pm_.waiting && pm_.tries == 0) {  // the SOS that preempted the clip
    main_.kind = ACT_MSG;
    main_.due = now;
  }
  apply_profile();
}

void NodeFsm::vt_tick(uint32_t now) {
  if (vt_.sub == VT_GUARD && reached(now, vt_.deadline)) {
    vt_.sub = VT_BURST;
    vt_.cur_frag = lowest_bit(vt_.sendmask);
    drop_acks_not(PROFILE_VOICE_C2_1300);
    main_.kind = ACT_FRAG;
    main_.due = now;
  } else if (vt_.sub == VT_COOLDOWN && reached(now, vt_.deadline)) {
    vt_start_attempt(now);
  } else if (vt_.sub == VT_WAIT && reached(now, vt_.deadline)) {
    if (!vt_.got_resp && vt_.round == 0 && vt_.attempt < MAX_CLIP_ATTEMPTS) {
      vt_.sub = VT_COOLDOWN;  // back to the alarm profile: lets an SOS in before the next burst
      vt_.announced = false;
      vt_.deadline = now + RETRY_LISTEN_MS;
      apply_profile();
    } else {
      vt_finish(vt_.got_resp ? CLIPTX_INCOMPLETE : CLIPTX_NO_RESPONSE, now);
    }
  }
}

// ---- voice rx -----------------------------------------------------------------------------

uint32_t NodeFsm::burst_time_ms(uint16_t mask) const {
  uint32_t t = 0;
  for (uint8_t i = 0; i < clip_.frag_cnt(); i++)
    if (mask & (1u << i))
      t += toa_ms(PROFILE_VOICE_C2_1300, HEADER_LEN + frag_pairs(clip_.pairs(), i) * PAIR_BYTES) + FRAG_GAP_MS;
  return t;
}

void NodeFsm::vr_queue_reply(TxKind kind, uint8_t status, uint16_t mask, uint32_t now) {
  uint8_t buf[MAX_PACKET];
  size_t n;
  if (kind == TXK_NACK) n = build_nack(buf, id_, vr_.src, clip_.msg_id(), mask);
  else n = build_ack(buf, id_, vr_.src, clip_.msg_id(), T_VANN, status, vr_.rssi, vr_.snr);
  queue_ack(kind, PROFILE_VOICE_C2_1300, now + ack_delay_ms(PROFILE_VOICE_C2_1300, vr_.dst_bcast), buf, n);
}

void NodeFsm::vr_complete(uint32_t now) {
  Event e;
  e.kind = EV_CLIP_RX_DONE;
  e.src = vr_.src;
  e.msg_id = clip_.msg_id();
  e.code = vr_.duplicate ? CLIPRX_DUPLICATE : CLIPRX_COMPLETE;
  e.tries = vr_.rounds;
  e.rssi = vr_.rssi;
  e.snr = vr_.snr;
  last_vann_valid_ = true;
  last_vann_src_ = vr_.src;
  last_vann_id_ = clip_.msg_id();
  emit(e);
  vr_queue_reply(TXK_ACK, ACK_OK, 0, now);
  vr_.closing = true;
  vr_.close_deadline = now + VR_CLOSE_FALLBACK_MS;
}

void NodeFsm::vr_finish_partial(bool send_ack, uint32_t now) {
  Event e;
  e.kind = EV_CLIP_RX_DONE;
  e.src = vr_.src;
  e.msg_id = clip_.msg_id();
  e.code = vr_.duplicate ? CLIPRX_DUPLICATE : CLIPRX_PARTIAL;
  e.mask = clip_.missing_mask();
  e.tries = vr_.rounds;
  e.rssi = vr_.rssi;
  e.snr = vr_.snr;
  last_vann_valid_ = true;
  last_vann_src_ = vr_.src;
  last_vann_id_ = clip_.msg_id();
  emit(e);
  if (send_ack) {
    vr_queue_reply(TXK_ACK, ACK_PARTIAL, 0, now);
    vr_.closing = true;
    vr_.close_deadline = now + VR_CLOSE_FALLBACK_MS;
  } else {
    vr_close(now);
  }
}

void NodeFsm::vr_abort(ClipRxEnd why, uint32_t) {
  Event e;
  e.kind = EV_CLIP_RX_ABORT;
  e.src = vr_.src;
  e.code = why;
  role_ = Role::NONE;
  vr_ = Vr();
  drop_acks_not(PROFILE_ALARM);
  emit(e);
  apply_profile();
}

void NodeFsm::vr_close(uint32_t) {
  role_ = Role::NONE;
  vr_ = Vr();
  apply_profile();
}

void NodeFsm::vr_tick(uint32_t now) {
  if (vr_.closing) {
    if (reached(now, vr_.close_deadline)) vr_close(now);
    return;
  }
  if (vr_.nack_queued) return;
  if (!vr_.got_any && vr_.rounds == 0) {
    if (reached(now, vr_.first_deadline)) vr_abort(CLIPRX_NO_SIGNAL, now);
    return;
  }
  if (vr_.rounds > 0 && vr_.frags_since_nack == 0 && reached(now, vr_.nack_done + RETX_WAIT_MS)) {
    vr_finish_partial(false, now);
    return;
  }
  uint32_t ready = vr_.last_rx + FRAG_IDLE_MS;
  uint32_t est = vr_.burst_end_est + NACK_MARGIN_MS;
  if (static_cast<int32_t>(est - ready) > 0) ready = est;
  if (!reached(now, ready)) return;
  if (vr_.rounds < MAX_NACK_ROUNDS) {
    vr_.rounds++;
    vr_.nack_queued = true;
    vr_queue_reply(TXK_NACK, 0, clip_.missing_mask(), now);
  } else {
    vr_finish_partial(true, now);
  }
}

// ---- main loop hooks ----------------------------------------------------------------------

void NodeFsm::on_tx_done(uint32_t now) {
  TxKind k = tx_kind_;
  tx_kind_ = TXK_NONE;
  switch (k) {
    case TXK_MSG:
      if (msg_tx_stale_) {
        msg_tx_stale_ = false;
      } else if (pm_.active) {
        pm_.tries++;
        pm_.waiting = true;
        pm_.deadline = now + ack_wait_ms(pm_.dst);
      }
      break;
    case TXK_ANN:
      vt_.announced = true;
      if (vt_.abort_req) {
        vt_do_abort(now);
      } else {
        vt_.sub = VT_GUARD;
        vt_.deadline = now + VOICE_GUARD_MS;
      }
      break;
    case TXK_FRAG:
      if (vt_.abort_req) {
        vt_do_abort(now);
        break;
      }
      vt_.sendmask &= static_cast<uint16_t>(~(1u << vt_.cur_frag));
      if (vt_.sendmask) {
        vt_.cur_frag = lowest_bit(vt_.sendmask);
        main_.kind = ACT_FRAG;
        main_.due = now + FRAG_GAP_MS;
      } else {
        vt_.sub = VT_WAIT;
        vt_.deadline = now + RESP_WAIT_MS;
      }
      break;
    case TXK_ABORT:
      vt_finish(vt_.abort_reason, now);
      break;
    case TXK_ACK:
      if (role_ == Role::VRX && vr_.closing) vr_close(now);
      break;
    case TXK_NACK:
      vr_.nack_queued = false;
      vr_.nack_done = now;
      vr_.frags_since_nack = 0;
      vr_.last_rx = now;
      vr_.burst_end_est = now + RETX_START_MS + burst_time_ms(clip_.missing_mask());
      break;
    default:
      break;
  }
  apply_profile();
  service_tx(now);
}

void NodeFsm::tick(uint32_t now) {
  if (pm_.active && pm_.waiting && reached(now, pm_.deadline)) {
    pm_.waiting = false;
    if (pm_.tries < pm_.max_tries) {
      main_.kind = ACT_MSG;
      main_.due = now;
    } else {
      pm_fail(0);
    }
  }
  if (role_ == Role::VTX) vt_tick(now);
  else if (role_ == Role::VRX) vr_tick(now);
  service_tx(now);
}

uint32_t NodeFsm::ms_until_work(uint32_t now) const {
  if (role_ != Role::NONE) return 1;
  uint32_t best = NEVER;
  auto consider = [&](uint32_t t) {
    uint32_t d = reached(now, t) ? 0 : t - now;
    if (d < best) best = d;
  };
  for (int i = 0; i < ACK_Q; i++)
    if (ackq_[i].used) consider(ackq_[i].due);
  if (main_.kind != ACT_NONE) consider(main_.due);
  if (pm_.active && pm_.waiting) consider(pm_.deadline);
  return best;
}

}  // namespace lr
