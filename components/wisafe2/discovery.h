#pragma once

#include "registry.h"
#include "scheduler.h"

#include <cstddef>
#include <cstdint>

namespace esphome {
namespace wisafe2 {

// Periodische Abfragen beginnen frühestens so lange nach dem Start, damit die Erkundung beim Start Vorrang hat.
static constexpr uint32_t PERIODIC_START_DELAY_MS = 15u * 60u * 1000u;
// Steht kein passender Knoten an, prüft die periodische Planung nach dieser Zeit erneut.
static constexpr uint32_t PERIODIC_RETRY_MS = 60u * 1000u;

// Nur in der Netzerkundung: D3 06 sid 00 wie QUERY_REMOTE_STATUS, aber nur, solange die Geräte-ID fehlt.
static constexpr uint8_t QUERY_LOCATE = 0x20;
// Kennung für nodes.bin: Geräte-ID über D4 06, falls unbekannt, dann Klasse und Modell über C4.
static constexpr uint8_t QUERY_IDENTIFY = QUERY_LOCATE | QUERY_IDENTITY;
// Netzdetails: Werte, die sich ändern können.
static constexpr uint8_t QUERY_DETAILS = QUERY_REMOTE_STATUS | QUERY_BATTERY | QUERY_FAULT_DETAILS | QUERY_FAULT_EVENTS;

struct DiscoveryTask {
  uint8_t sid{NO_SID};
  uint8_t query{0};  // ein Bit aus QUERY_*
};

// Plant die Abfragen von Erkundung und Netzdetails. Je SID laufen die Schritte nacheinander: D4 06, C4 bei
// unbekannter Klasse, dann C8 und 73 bei Meldern, C6 nur bei WST-630 und ST-630-DE. Gesendet wird immer nur eine
// Abfrage; die Sendeplanung hält ohnehin höchstens eine Fernanfrage offen.
class Discovery {
 public:
  enum class Scope : uint8_t {
    Identify,  // Erkundung: Kennung der SIDs ohne vollständigen Eintrag
    Details,   // Netzdetails: Remote-Status aller SIDs, Batterie und Fehler der Melder
  };

  // Startet die Abfragen mit der nächsten lokalen SID-Map (D4 03). Beide Arten lassen sich gemeinsam anfordern.
  void request(Scope scope);
  bool waiting_for_map() const { return this->waiting_; }
  // Legt die Ziele aus der SID-Map fest, ohne die eigene SID. Rückgabe: Anzahl der Ziel-SIDs.
  int on_map(uint64_t map, const Registry &registry, uint8_t own_sid);
  // Einzelne Abfragen außerhalb einer Erkundung, etwa 72 und C5 nach 71 mit Flag 0x02.
  void want(uint8_t sid, uint8_t queries);
  // Nächste Abfrage. Schritte ohne Voraussetzung entfallen. false, wenn nichts ansteht oder eine offen ist.
  bool next(const Registry &registry, DiscoveryTask &task);
  // Ergebnis der offenen Abfrage. Ohne D4 06 entfallen die übrigen Schritte dieser SID.
  void on_result(uint8_t sid, uint8_t query, Outcome outcome);
  bool has_work() const;
  bool busy() const { return this->active_; }
  // Genau einmal true, wenn eine mit on_map begonnene Erkundung abgeschlossen ist.
  bool take_finished();
  bool campaign_running() const { return this->campaign_; }
  // „Erkundung“, „Netzdetails“ oder beides; gilt bis zum Start der nächsten Runde.
  const char *campaign_name() const;
  int campaign_total() const { return this->total_; }
  int campaign_index() const { return this->index_; }
  void clear();

  // Periodische Abfragen (auto_discovery): D4 06 je SID und C8 je Melder, gleichmäßig über das Intervall
  // verteilt. 0 schaltet ab.
  void set_intervals(uint32_t remote_status_ms, uint32_t battery_ms);
  void tick(uint32_t now_ms, const Registry &registry, uint8_t own_sid);

 protected:
  bool allowed_(const Registry &registry, uint8_t sid, uint8_t query) const;
  void finish_if_done_();
  uint8_t pick_(const Registry &registry, uint8_t own_sid, bool battery, uint8_t &cursor, int &count) const;

  uint8_t wanted_[MAX_SID + 1]{};
  bool waiting_{false};
  bool want_identify_{false};
  bool want_details_{false};
  bool active_{false};
  DiscoveryTask active_task_{};
  uint8_t current_sid_{NO_SID};

  bool campaign_{false};
  bool campaign_identify_{false};
  bool campaign_details_{false};
  uint64_t campaign_pending_{0};
  int total_{0};
  int index_{0};
  bool finished_{false};

  uint32_t status_interval_ms_{0};
  uint32_t battery_interval_ms_{0};
  bool periodic_started_{false};
  uint32_t next_status_ms_{0};
  uint32_t next_battery_ms_{0};
  uint8_t status_cursor_{MAX_SID};
  uint8_t battery_cursor_{MAX_SID};
};

// QUERY_* zu einem Befehl: D3 06 sid 00/01, C7, 72, C5. 0 für andere Befehle. QUERY_LOCATE ergibt sich nie,
// D3 06 sid 00 ist immer QUERY_REMOTE_STATUS.
uint8_t query_for_payload(const uint8_t *payload, size_t len);
const char *query_name(uint8_t query);

}  // namespace wisafe2
}  // namespace esphome
