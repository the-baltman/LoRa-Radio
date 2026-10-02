// LoRa-Radio packet format (protocol v1). Portable C++11: no Arduino, no heap.
// Wire format and rationale: docs/protocolo.md. All multi-byte fields are little-endian.
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <stddef.h>
#include <stdint.h>

namespace lr {

// ---- Identity ---------------------------------------------------------------------------
constexpr uint16_t NET_ID = 0x4C52;       // "LR": rejects foreign LoRa traffic with the same PHY settings
constexpr uint8_t PROTO_VERSION = 1;      // high nibble of byte 2
constexpr uint8_t SYNC_WORD = 0x6B;       // private; NOT 0x12 (RadioLib default), 0x34 (LoRaWAN), 0x2B (Meshtastic [PV])
constexpr uint8_t NODE_INVALID = 0x00;
constexpr uint8_t NODE_BROADCAST = 0xFF;

// ---- Sizes ------------------------------------------------------------------------------
constexpr size_t HEADER_LEN = 8;
constexpr size_t MAX_PACKET = 255;         // SX126x payload limit [B]
constexpr size_t PAIR_BYTES = 13;          // 2 Codec2-1300 frames (2 x 52 bit) packed into 104 bit
constexpr size_t FRAG_PAIRS = 19;          // pairs per full fragment: 19 x 13 = 247 B
constexpr size_t FRAG_AUDIO_BYTES = FRAG_PAIRS * PAIR_BYTES;  // 247 B -> packet of 255 B
constexpr uint8_t MAX_FRAGS = 15;          // 4-bit count in the header
constexpr uint32_t PAIR_MS = 80;           // audio duration of one pair [ms] (2 x 40 ms)

static_assert(HEADER_LEN + FRAG_AUDIO_BYTES == MAX_PACKET, "full fragment must fill a LoRa packet");

// ---- Packet types (low nibble of byte 2) ------------------------------------------------
// 1..5 are the five closed-list messages; the type IS the 1-byte message code.
enum PacketType : uint8_t {
  T_NONE = 0,
  T_SOS = 1,
  T_PELIGRO = 2,
  T_PRUEBA = 3,
  T_SI = 4,
  T_NO = 5,
  T_ACK = 6,
  T_NACK = 7,
  T_VANN = 8,    // voice announce (sent in the alarm profile)
  T_VFRAG = 9,   // voice fragment (sent in the voice profile)
  T_VABORT = 10, // sender cancels the clip (voice profile)
};

inline bool is_message_type(uint8_t t) { return t >= T_SOS && t <= T_NO; }

// Text shown on the OLED (< 16 chars, ASCII only: the OLED font has no accents).
inline const char* message_text(uint8_t t) {
  switch (t) {
    case T_SOS: return "SOS / AYUDA";
    case T_PELIGRO: return "PELIGRO";
    case T_PRUEBA: return "PRUEBA";
    case T_SI: return "SI";
    case T_NO: return "NO";
    default: return "?";
  }
}

// ---- Radio profiles ---------------------------------------------------------------------
// The SX126x demodulates ONE SF/BW at a time [scientist 4.3]. Idle = alarm profile.
enum ProfileId : uint8_t {
  PROFILE_ALARM = 0,          // SF12 / BW500 / CR4/5: messages, ANN, ACK. Longest range legal in MX.
  PROFILE_VOICE_C2_1300 = 1,  // SF9 / BW500 / CR4/5 + Codec2 1300: clip fragments, NACK, clip ACK
};

// ---- Header -----------------------------------------------------------------------------
// off len field
//  0   2  net_id
//  2   1  version(4) | type(4)
//  3   1  src      node id 1..254
//  4   1  dst      node id or 0xFF broadcast
//  5   2  msg_id   per-source counter; retries of one message keep the same value
//  7   1  frag     VFRAG only: index(4) | count(4); 0 otherwise
struct Header {
  uint16_t net = NET_ID;
  uint8_t ver = PROTO_VERSION;
  uint8_t type = T_NONE;
  uint8_t src = NODE_INVALID;
  uint8_t dst = NODE_BROADCAST;
  uint16_t msg_id = 0;
  uint8_t frag_idx = 0;
  uint8_t frag_cnt = 0;
};

enum ParseStatus : uint8_t {
  PARSE_OK = 0,
  PARSE_TOO_SHORT,
  PARSE_BAD_NET,
  PARSE_BAD_VERSION,
  PARSE_BAD_TYPE,
  PARSE_BAD_ADDR,
  PARSE_BAD_PAYLOAD,
};

struct Packet {
  Header h;
  const uint8_t* payload = nullptr;  // points into the caller's buffer
  uint8_t plen = 0;
};

// Payload structures ------------------------------------------------------------------------
enum AckStatus : uint8_t { ACK_OK = 0, ACK_PARTIAL = 1 };

struct AckInfo {
  uint8_t acked_type;
  uint8_t status;     // AckStatus
  int8_t rssi_dbm;    // RSSI the acker measured on the acked packet
  float snr_db;       // SNR the acker measured (0.25 dB resolution)
};

struct VannInfo {
  uint8_t profile;    // ProfileId of the burst
  uint8_t frag_cnt;
  uint16_t pairs;     // total pairs in the clip
};

// Builders return the packet length (0 on bad arguments). `out` must hold MAX_PACKET bytes.
size_t build_message(uint8_t* out, uint8_t type, uint8_t src, uint8_t dst, uint16_t msg_id);
size_t build_ack(uint8_t* out, uint8_t src, uint8_t dst, uint16_t msg_id, uint8_t acked_type,
                 uint8_t status, float rssi_dbm, float snr_db);
size_t build_nack(uint8_t* out, uint8_t src, uint8_t dst, uint16_t msg_id, uint16_t missing_mask);
size_t build_vann(uint8_t* out, uint8_t src, uint8_t dst, uint16_t msg_id, uint8_t profile,
                  uint8_t frag_cnt, uint16_t pairs);
size_t build_vfrag(uint8_t* out, uint8_t src, uint8_t dst, uint16_t msg_id, uint8_t idx, uint8_t cnt,
                   const uint8_t* audio, uint8_t pairs);
size_t build_vabort(uint8_t* out, uint8_t src, uint8_t dst, uint16_t msg_id);

ParseStatus parse_packet(const uint8_t* buf, size_t len, Packet* out);
bool parse_ack(const Packet& p, AckInfo* out);
bool parse_nack(const Packet& p, uint16_t* missing_mask);
bool parse_vann(const Packet& p, VannInfo* out);

// Counter arithmetic (16-bit wrap-around) ----------------------------------------------------
// Packets skipped between two consecutive ids of one source. Returns -1 when `cur` is not newer
// than `prev` (duplicate or reordered).
inline int32_t seq_gap(uint16_t prev, uint16_t cur) {
  uint16_t d = static_cast<uint16_t>(cur - prev);
  if (d == 0 || d >= 0x8000) return -1;
  return static_cast<int32_t>(d) - 1;
}

// Number of fragments needed for `pairs` pairs.
inline uint8_t frag_count_for(uint16_t pairs) {
  return static_cast<uint8_t>((pairs + FRAG_PAIRS - 1) / FRAG_PAIRS);
}
// Pairs carried by fragment `idx` of a clip of `pairs` pairs (0 if out of range).
inline uint16_t frag_pairs(uint16_t pairs, uint8_t idx) {
  uint8_t cnt = frag_count_for(pairs);
  if (idx >= cnt) return 0;
  if (idx + 1u < cnt) return FRAG_PAIRS;
  return static_cast<uint16_t>(pairs - FRAG_PAIRS * (cnt - 1u));
}

}  // namespace lr
