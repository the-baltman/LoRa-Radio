// SPDX-License-Identifier: GPL-3.0-or-later
#include "protocol.h"

#include <string.h>

namespace lr {

namespace {

void put_header(uint8_t* out, uint8_t type, uint8_t src, uint8_t dst, uint16_t msg_id, uint8_t frag) {
  out[0] = static_cast<uint8_t>(NET_ID & 0xFF);
  out[1] = static_cast<uint8_t>(NET_ID >> 8);
  out[2] = static_cast<uint8_t>((PROTO_VERSION << 4) | (type & 0x0F));
  out[3] = src;
  out[4] = dst;
  out[5] = static_cast<uint8_t>(msg_id & 0xFF);
  out[6] = static_cast<uint8_t>(msg_id >> 8);
  out[7] = frag;
}

bool valid_src(uint8_t src) { return src != NODE_INVALID && src != NODE_BROADCAST; }

int8_t clamp_rssi(float rssi_dbm) {
  if (rssi_dbm > 0.0f) return 0;
  if (rssi_dbm < -127.0f) return -127;
  return static_cast<int8_t>(rssi_dbm - 0.5f);  // truncation of (x - 0.5) = round to nearest for x < 0
}

int8_t snr_to_q2(float snr_db) {
  float q = snr_db * 4.0f;
  if (q > 127.0f) q = 127.0f;
  if (q < -127.0f) q = -127.0f;
  return static_cast<int8_t>(q >= 0 ? q + 0.5f : q - 0.5f);
}

}  // namespace

size_t build_message(uint8_t* out, uint8_t type, uint8_t src, uint8_t dst, uint16_t msg_id) {
  if (!is_message_type(type) || !valid_src(src) || dst == NODE_INVALID) return 0;
  put_header(out, type, src, dst, msg_id, 0);
  return HEADER_LEN;
}

size_t build_ack(uint8_t* out, uint8_t src, uint8_t dst, uint16_t msg_id, uint8_t acked_type,
                 uint8_t status, float rssi_dbm, float snr_db) {
  if (!valid_src(src) || dst == NODE_INVALID || dst == NODE_BROADCAST) return 0;
  put_header(out, T_ACK, src, dst, msg_id, 0);
  out[8] = acked_type;
  out[9] = status;
  out[10] = static_cast<uint8_t>(clamp_rssi(rssi_dbm));
  out[11] = static_cast<uint8_t>(snr_to_q2(snr_db));
  return HEADER_LEN + 4;
}

size_t build_nack(uint8_t* out, uint8_t src, uint8_t dst, uint16_t msg_id, uint16_t missing_mask) {
  if (!valid_src(src) || dst == NODE_INVALID || dst == NODE_BROADCAST) return 0;
  put_header(out, T_NACK, src, dst, msg_id, 0);
  out[8] = static_cast<uint8_t>(missing_mask & 0xFF);
  out[9] = static_cast<uint8_t>(missing_mask >> 8);
  return HEADER_LEN + 2;
}

size_t build_vann(uint8_t* out, uint8_t src, uint8_t dst, uint16_t msg_id, uint8_t profile,
                  uint8_t frag_cnt, uint16_t pairs) {
  if (!valid_src(src) || dst == NODE_INVALID) return 0;
  if (frag_cnt == 0 || frag_cnt > MAX_FRAGS || pairs == 0) return 0;
  put_header(out, T_VANN, src, dst, msg_id, 0);
  out[8] = profile;
  out[9] = frag_cnt;
  out[10] = static_cast<uint8_t>(pairs & 0xFF);
  out[11] = static_cast<uint8_t>(pairs >> 8);
  return HEADER_LEN + 4;
}

size_t build_vfrag(uint8_t* out, uint8_t src, uint8_t dst, uint16_t msg_id, uint8_t idx, uint8_t cnt,
                   const uint8_t* audio, uint8_t pairs) {
  if (!valid_src(src) || dst == NODE_INVALID) return 0;
  if (cnt == 0 || cnt > MAX_FRAGS || idx >= cnt) return 0;
  if (pairs == 0 || pairs > FRAG_PAIRS || audio == nullptr) return 0;
  put_header(out, T_VFRAG, src, dst, msg_id, static_cast<uint8_t>((idx << 4) | cnt));
  memcpy(out + HEADER_LEN, audio, pairs * PAIR_BYTES);
  return HEADER_LEN + pairs * PAIR_BYTES;
}

size_t build_vabort(uint8_t* out, uint8_t src, uint8_t dst, uint16_t msg_id) {
  if (!valid_src(src) || dst == NODE_INVALID) return 0;
  put_header(out, T_VABORT, src, dst, msg_id, 0);
  return HEADER_LEN;
}

ParseStatus parse_packet(const uint8_t* buf, size_t len, Packet* out) {
  if (len < HEADER_LEN || len > MAX_PACKET) return PARSE_TOO_SHORT;
  Header h;
  h.net = static_cast<uint16_t>(buf[0] | (buf[1] << 8));
  if (h.net != NET_ID) return PARSE_BAD_NET;
  h.ver = buf[2] >> 4;
  if (h.ver != PROTO_VERSION) return PARSE_BAD_VERSION;
  h.type = buf[2] & 0x0F;
  if (h.type == T_NONE || h.type > T_VABORT) return PARSE_BAD_TYPE;
  h.src = buf[3];
  h.dst = buf[4];
  if (!valid_src(h.src) || h.dst == NODE_INVALID) return PARSE_BAD_ADDR;
  h.msg_id = static_cast<uint16_t>(buf[5] | (buf[6] << 8));
  uint8_t plen = static_cast<uint8_t>(len - HEADER_LEN);

  if (h.type == T_VFRAG) {
    h.frag_idx = buf[7] >> 4;
    h.frag_cnt = buf[7] & 0x0F;
    if (h.frag_cnt == 0 || h.frag_idx >= h.frag_cnt) return PARSE_BAD_PAYLOAD;
    if (plen == 0 || plen % PAIR_BYTES != 0 || plen / PAIR_BYTES > FRAG_PAIRS) return PARSE_BAD_PAYLOAD;
  }
  // ACK/NACK are replies to one node: a broadcast destination is malformed.
  if ((h.type == T_ACK && (plen < 4 || h.dst == NODE_BROADCAST)) ||
      (h.type == T_NACK && (plen < 2 || h.dst == NODE_BROADCAST)) || (h.type == T_VANN && plen < 4)) {
    return PARSE_BAD_PAYLOAD;
  }
  out->h = h;
  out->payload = buf + HEADER_LEN;
  out->plen = plen;
  return PARSE_OK;
}

bool parse_ack(const Packet& p, AckInfo* out) {
  if (p.h.type != T_ACK || p.plen < 4) return false;
  out->acked_type = p.payload[0];
  out->status = p.payload[1];
  out->rssi_dbm = static_cast<int8_t>(p.payload[2]);
  out->snr_db = static_cast<int8_t>(p.payload[3]) * 0.25f;
  return true;
}

bool parse_nack(const Packet& p, uint16_t* missing_mask) {
  if (p.h.type != T_NACK || p.plen < 2) return false;
  *missing_mask = static_cast<uint16_t>(p.payload[0] | (p.payload[1] << 8));
  return true;
}

bool parse_vann(const Packet& p, VannInfo* out) {
  if (p.h.type != T_VANN || p.plen < 4) return false;
  out->profile = p.payload[0];
  out->frag_cnt = p.payload[1];
  out->pairs = static_cast<uint16_t>(p.payload[2] | (p.payload[3] << 8));
  return true;
}

}  // namespace lr
