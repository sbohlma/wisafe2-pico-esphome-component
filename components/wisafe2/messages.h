#pragma once

#include "framing.h"

#include <cstddef>
#include <cstdint>

namespace esphome {
namespace wisafe2 {

static constexpr uint8_t MSG_ACK = 0x46;
static constexpr uint8_t MSG_REJECT = 0x47;
static constexpr uint8_t MSG_IDENTITY_REQUEST = 0x41;
static constexpr uint8_t MSG_RM_STATUS = 0xD2;
static constexpr uint8_t MSG_RM_REPORT = 0xD4;
static constexpr uint8_t SID_UNPAIRED = 0x40;

// Antworten des eigenen Funkmoduls (RM) an den Host.
enum class LocalType : uint8_t {
  None,
  Ack,              // 46: Funkbefehl angenommen
  Reject,           // 47: Ziel-SID nicht in der lokalen SID-Map
  IdentityRequest,  // 41: RM fragt den Host nach seiner Identität
  RmStatus,         // D2: Status des eigenen RM
  MissingMap,       // D4 01
  LocalSidMap,      // D4 03
  NewNetworkMap,    // D4 04
  RemoteStatus,     // D4 06
  PresenceHeard,    // D4 09
  RemoteSidMap,     // D4 11
};

// Gemeinsame Felder von D2 (d1–d12) und D4 06 (d3–d12, dort zusätzlich die SID in d2).
struct RmState {
  uint8_t bvnl{0};
  uint8_t bvwl{0};
  uint8_t unknown{0};
  uint8_t rssi{0};
  uint8_t firmware{0};
  uint8_t device_id[3]{};
  uint8_t critical{0};
  uint8_t radio_faults{0};
};

struct LocalMessage {
  LocalType type{LocalType::None};
  RmState rm{};             // RmStatus, RemoteStatus
  uint8_t sid{0};           // RmStatus, RemoteStatus, PresenceHeard, RemoteSidMap
  uint8_t rm_status_d12{0};  // RmStatus
  uint8_t reason{0};        // MissingMap: 00 nach D3 06, 04 nach D3 04
  uint64_t map{0};          // MissingMap, LocalSidMap, NewNetworkMap, RemoteSidMap
  uint8_t device_id[3]{};   // PresenceHeard
};

// false, wenn der Rahmen keine bekannte lokale Antwort ist.
bool parse_local(const Frame &frame, LocalMessage &out);

// RM-Batterie in mV aus BVNL: U = 1048,576 / (BVNL + 285) V.
uint32_t rm_battery_mv(uint8_t bvnl);

// SID-Map aus 8 Bytes; Byte 0 enthält die SIDs 0–7, das niedrigste Bit ist die kleinste SID.
uint64_t read_sid_map(const uint8_t *bytes);
int count_sids(uint64_t map);

// Geräte-ID in Anzeigereihenfolge, wie das Gateway sie zeigt: Paket 3D E4 2F ergibt "2FE43D".
void format_device_id(const uint8_t *id, char *out, size_t out_len);

}  // namespace wisafe2
}  // namespace esphome
