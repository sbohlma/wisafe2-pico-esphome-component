#pragma once

#include "messages.h"
#include "scheduler.h"

#include <cstddef>
#include <cstdint>

namespace esphome {
namespace wisafe2 {

// Netzvorgabe: höchstens 50 Knoten.
static constexpr size_t REGISTRY_SLOTS = 50;
// nodes.bin: Kopf mit 8 Byte und 50 Plätze zu je 8 Byte.
static constexpr size_t REGISTRY_HEADER_LEN = 8;
static constexpr size_t REGISTRY_SLOT_LEN = 8;
static constexpr size_t REGISTRY_FILE_LEN = REGISTRY_HEADER_LEN + REGISTRY_SLOTS * REGISTRY_SLOT_LEN;
static constexpr uint8_t REGISTRY_FORMAT_VERSION = 1;

// Flags je Platz, so auch in nodes.bin.
static constexpr uint8_t NODE_GATEWAY = 0x01;       // Gateway-RM, Geräte-ID 01 02 03
static constexpr uint8_t NODE_TYPE_UNKNOWN = 0x02;  // RM ohne Melder: antwortet auf D4 06, liefert kein C4
static constexpr uint8_t NODE_OWN = 0x04;           // eigenes RM der Brücke

// Abfragen an einen Knoten; dieselben Bits plant die Netzerkundung.
static constexpr uint8_t QUERY_REMOTE_STATUS = 0x01;  // D3 06 sid 00 -> D4 06
static constexpr uint8_t QUERY_IDENTITY = 0x02;       // D3 06 sid 01 -> C4
static constexpr uint8_t QUERY_BATTERY = 0x04;        // C7 -> C8
static constexpr uint8_t QUERY_FAULT_DETAILS = 0x08;  // 72 -> 73
static constexpr uint8_t QUERY_FAULT_EVENTS = 0x10;   // C5 -> C6
static constexpr uint8_t QUERY_ALL = 0x1F;

// Rückgabe von apply() und on_query_result().
static constexpr uint8_t CHANGE_NEW = 0x01;       // neuer Platz
static constexpr uint8_t CHANGE_IDENTITY = 0x02;  // Geräte-ID, Modell, Klasse oder Flags geändert (speichern)
static constexpr uint8_t CHANGE_STATE = 0x04;     // Zustand im RAM geändert
static constexpr uint8_t CHANGE_REACH = 0x08;     // Erreichbarkeit hat gewechselt
static constexpr uint8_t CHANGE_DEVICE = 0x10;    // Geräte-ID eines bekannten Knotens hat gewechselt

enum class Reach : uint8_t { Unknown, Reachable, Unreachable };
// Gilt nach dem Start als unbekannt und wechselt nur durch 50 und 51.
enum class AlarmState : uint8_t { Unknown, Active, Ended };

struct Node {
  uint8_t sid{0xFF};
  // Gespeichert in nodes.bin. 000000, Modell 0 und Klasse 0 heißen unbekannt.
  uint8_t flags{0};
  uint8_t device_id[3]{};
  uint16_t model{0};
  uint8_t device_class{0};

  // Nur im RAM.
  bool in_network{false};  // in der letzten D4 03 oder D4 04
  Reach reach{Reach::Unknown};
  uint8_t missed{0};  // ausgebliebene Antworten in Folge
  bool seen{false};
  uint32_t last_seen_ms{0};

  bool has_rm{false};  // D4 06 bzw. D2 beim eigenen RM
  uint8_t rm_bvnl{0};
  uint8_t rm_rssi{0};
  uint8_t rm_firmware{0};
  uint8_t rm_critical{0};
  uint8_t rm_radio_faults{0};

  bool has_battery{false};  // C8
  uint16_t battery_mv{0};
  bool has_fault_flags{false};  // 71
  uint8_t fault_flags{0};
  bool has_fault_details{false};  // 73
  uint16_t fault_details{0};
  bool has_fault_events{false};  // C6
  uint8_t fault_events[3]{};
  bool has_test{false};  // 70
  uint8_t test_result{0};
  uint32_t test_ms{0};

  AlarmState alarm{AlarmState::Unknown};
  uint8_t alarm_class{0};
  bool silenced{false};

  bool has_device() const { return device_id[0] != 0 || device_id[1] != 0 || device_id[2] != 0; }
  bool has_model() const { return model != 0; }
  bool has_class() const { return device_class != 0; }
  bool is_gateway() const { return (flags & NODE_GATEWAY) != 0; }
  bool is_own() const { return (flags & NODE_OWN) != 0; }
  // Melder mit Geräte-ID und Modell, die C7 und 72 beantworten. Schnittstellen (W2-TSL, IFG-100, IFG-200)
  // melden keine Melderfehler.
  bool is_detector() const;
  // Melder, deren C8 eine Batteriespannung enthält. Der WHT-630 liefert dort keinen Wert (P13).
  bool has_battery_report() const;
  // C5 beantworten nur WST-630 und ST-630-DE.
  bool has_fault_events_support() const;
  // Vollständiger Eintrag: Geräte-ID und Klasse bekannt, oder als Gateway bzw. RM ohne Melder erkannt.
  bool complete() const;
};

enum class LoadResult : uint8_t { Ok, BadLength, BadMagic, BadVersion, BadCrc, BadContent };
const char *load_result_name(LoadResult result);

// Melderregister je SID. Kommt ohne ESPHome-Header aus und läuft in den Host-Tests.
class Registry {
 public:
  // nullptr, wenn die SID keinen Platz hat.
  Node *find(uint8_t sid);
  const Node *find(uint8_t sid) const;
  const Node *find_device(const uint8_t *device_id) const;
  // Melder aus der YAML: über die Geräte-ID, sonst über die SID. Ist eine Geräte-ID angegeben, gilt der Platz der
  // SID nur, solange dort keine andere Geräte-ID steht. device_id nullptr oder 000000 heißt: nur SID.
  const Node *resolve(uint8_t sid, const uint8_t *device_id) const;
  // Active, sobald ein Knoten im Alarm ist; Ended, wenn mindestens ein Alarm endete und keiner aktiv ist.
  AlarmState network_alarm() const;
  bool any_silenced() const;
  // Legt einen leeren Platz an. nullptr bei ungültiger SID oder vollem Register.
  Node *get_or_add(uint8_t sid, uint8_t &changes);

  // Passive Nachrichten und Antworten aus dem Netz. Anfragen (72, C1, C3, C5, C7) legen keinen Platz an.
  uint8_t apply(const RadioMessage &msg, uint32_t now_ms);
  // D2 (eigenes RM), D4 01, D4 03, D4 04, D4 06, D4 09.
  uint8_t apply(const LocalMessage &msg, uint32_t now_ms);
  // Eigene Identität aus der YAML für den Platz der eigenen SID.
  uint8_t set_own(uint8_t sid, const uint8_t *device_id, uint16_t model, uint8_t device_class);
  // Ergebnis einer Abfrage an einen Knoten (E8): D4 01 gilt sofort als nicht erreichbar, zwei ausgebliebene
  // Antworten in Folge ebenso. Ein ausgebliebenes C4 bei bekanntem RM kennzeichnet ein RM ohne Melder.
  uint8_t on_query_result(uint8_t sid, uint8_t query, Outcome outcome);

  void clear();
  size_t count() const { return this->count_; }
  const Node &at(size_t index) const { return this->nodes_[index]; }
  bool known(uint8_t sid) const;
  uint64_t members() const;

  // nodes.bin; out_len mindestens REGISTRY_FILE_LEN. Gespeichert werden Plätze mit Geräte-ID oder Flags.
  size_t serialize(uint8_t *out, size_t out_len) const;
  // Bei einem Fehler bleibt das Register leer.
  LoadResult deserialize(const uint8_t *data, size_t len);

 protected:
  void seen_(Node &node, uint32_t now_ms, uint8_t &changes);
  void set_device_(Node &node, const uint8_t *id, uint8_t &changes);
  void set_model_(Node &node, uint16_t model, uint8_t &changes);
  void set_class_(Node &node, uint8_t device_class, uint8_t &changes);
  void set_flag_(Node &node, uint8_t flag, bool on, uint8_t &changes);
  void set_unreachable_(Node &node, uint8_t &changes);
  uint8_t apply_membership_(uint64_t map);

  Node nodes_[REGISTRY_SLOTS]{};
  size_t count_{0};
};

// CRC-16/CCITT-FALSE (Polynom 0x1021, Start 0xFFFF).
uint16_t crc16_ccitt(const uint8_t *data, size_t len);

// Eine Zeile je Knoten für das Log, etwa
// "SID 14: 2FE43D ST-630-DE, Rauch, im Netz, erreichbar, Batterie 2954 mV, ...".
void describe_node(const Node &node, uint32_t now_ms, char *out, size_t out_len);

}  // namespace wisafe2
}  // namespace esphome
