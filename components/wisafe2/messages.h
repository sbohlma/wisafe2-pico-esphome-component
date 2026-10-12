#pragma once

#include "framing.h"

#include <cstddef>
#include <cstdint>

namespace esphome {
namespace wisafe2 {

static constexpr uint8_t MSG_ACK = 0x46;
static constexpr uint8_t MSG_REJECT = 0x47;
static constexpr uint8_t MSG_IDENTITY_REQUEST = 0x41;
static constexpr uint8_t MSG_IDENTITY_REPLY = 0x91;
static constexpr uint8_t MSG_RM_STATUS = 0xD2;
static constexpr uint8_t MSG_RM_REPORT = 0xD4;
static constexpr uint8_t SID_UNPAIRED = 0x40;
static constexpr uint8_t MAX_SID = 63;

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
  InitReport,       // D4 10: Knoten, die nach dem Init geantwortet haben (vermutet), und eigene SID
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
  uint8_t sid{0};           // RmStatus, RemoteStatus, PresenceHeard, InitReport, RemoteSidMap
  uint8_t rm_status_d12{0};  // RmStatus
  uint8_t reason{0};        // MissingMap: 00 nach D3 06, 04 nach D3 04
  uint64_t map{0};          // MissingMap, LocalSidMap, NewNetworkMap, InitReport, RemoteSidMap
  uint8_t device_id[3]{};   // PresenceHeard
};

// false, wenn der Rahmen keine bekannte lokale Antwort ist.
bool parse_local(const Frame &frame, LocalMessage &out);

// Nachrichten aus dem Netz. Das sendende RM hängt seine SID und eine Sequenznummer an; beide werden von
// hinten gelesen, weil manche Absender davor noch ein Byte 7E einfügen.
enum class RadioType : uint8_t {
  None,
  Alarm,                // 50
  AlarmStop,            // 51
  Silence,              // 61
  Test,                 // 70
  Fault,                // 71
  FaultDetailsRequest,  // 72, an jeden Host geflutet
  FaultDetails,         // 73
  DiagnoseRequest,      // C1
  DiagnoseResult,       // C2
  IdRequest,            // C3, aus D3 06 sid 01
  IdInfo,               // C4
  FaultEventsRequest,   // C5, an jeden Host geflutet
  FaultEvents,          // C6
  BatteryRequest,       // C7, an jeden Host geflutet
  Battery,              // C8
  UnknownD5,            // D5
};

struct RadioMessage {
  RadioType type{RadioType::None};
  uint8_t code{0};
  uint8_t sid{0};
  uint8_t seq{0};
  // Absender bzw. bei 72, C5 und C7 das Zielgerät.
  bool has_device{false};
  uint8_t device_id[3]{};
  bool has_model{false};
  uint16_t model{0};
  bool has_class{false};
  uint8_t device_class{0};
  // 50 d5, 61 d5, 70 Ergebnis, 71 Flags, 72/C5/C7 d6, C1 d7, C2 Ergebnis, C6/C8 Echo d4, D5 d6.
  uint8_t value{0};
  // 73 FS, C8 Batterie in mV.
  uint16_t value16{0};
  // C6 d5–d7.
  uint8_t events[3]{};
};

bool parse_radio(const Frame &frame, RadioMessage &out);
// 72, C1, C3, C5 und C7 verlangen eine Antwort vom Host.
bool is_host_request(RadioType type);

// RM-Batterie in mV aus BVNL: U = 1048,576 / (BVNL + 285) V.
uint32_t rm_battery_mv(uint8_t bvnl);

// SID-Map aus 8 Bytes; Byte 0 enthält die SIDs 0–7, das niedrigste Bit ist die kleinste SID.
uint64_t read_sid_map(const uint8_t *bytes);
int count_sids(uint64_t map);

// Geräte-ID in Anzeigereihenfolge, wie das Gateway sie zeigt: Paket 3D E4 2F ergibt "2FE43D".
void format_device_id(const uint8_t *id, char *out, size_t out_len);
// Umkehrung von format_device_id: "14B8A5" ergibt A5 B8 14. false bei ungültiger Eingabe.
bool parse_device_id(const char *text, uint8_t *id);

}  // namespace wisafe2
}  // namespace esphome
