#pragma once

#include "messages.h"
#include "registry.h"

#include <cstddef>
#include <cstdint>

namespace esphome {
namespace wisafe2 {

// Zustände für die Entitäten je Melder in Home Assistant (Stufe 3). Unknown heißt: Es kam noch keine
// passende Nachricht; die Entität bleibt dann ohne Wert.
enum class Tri : uint8_t { Unknown, Off, On };

// Alarm: wechselt nur durch 50 und 51.
Tri node_alarm(const Node &node);
// Fehler: 71 mit Bit 0x02 oder 73 mit FS ungleich 0.
Tri node_fault(const Node &node);
// Sockel-Problem: 71 ohne Bit 0x04, der Melder ist abgenommen.
Tri node_socket_problem(const Node &node);
// Melder-Batterie schwach: 71 mit Bit 0x08 oder das Batterie-Bit in 73.
Tri node_battery_low(const Node &node);
// RM-Batterie schwach: 71 mit Bit 0x20 oder das RM-Batterie-Bit in 73.
Tri node_rm_battery_low(const Node &node);
// Im Netz: in der SID-Map und nicht als unerreichbar erkannt.
bool node_online(const Node &node);

// "ST-630-DE, 2FE43D"; ohne Modell "Modell unbekannt, 2FE43D".
void describe_model(const Node &node, char *out, size_t out_len);
// "keine Fehler" oder die Fehlertexte je Bit, durch Komma getrennt.
void describe_fault_details(uint16_t model, uint16_t fault_details, char *out, size_t out_len);

// "bestanden" oder "fehlgeschlagen", mit Uhrzeit, wenn epoch gültig ist: "bestanden am 13.10. 14:03".
// Die Uhrzeit folgt der lokalen Zeitzone (TZ, gesetzt von der time-Komponente).
void describe_test(uint8_t result, int64_t epoch, char *out, size_t out_len);
// ISO 8601 in UTC für Text-Sensoren mit device_class timestamp, etwa "2026-10-13T14:03:00Z".
void format_timestamp(int64_t epoch, char *out, size_t out_len);
// Vor 2020 gilt die Systemzeit als nicht gestellt.
bool epoch_valid(int64_t epoch);

// Ereignistyp für die Event-Entität je Melder, nullptr für Nachrichten ohne Ereignis.
const char *event_type(RadioType type);
// Eine Zeile für „letztes Ereignis“, etwa "Alarm: SID 4 (Flur), Rauch". name darf nullptr sein.
void describe_event(const RadioMessage &msg, const char *name, char *out, size_t out_len);

// Knoten, die als Melder in die YAML gehören: alle außer eigenem RM, Gateway-RM und RM ohne Melder.
bool node_assignable(const Node &node);
// device_class der Alarm-Entität passend zur Klasse: "smoke", "heat", "carbon_monoxide"; sonst nullptr.
const char *alarm_device_class(uint8_t device_class);
// Die Vorlage nennt die Geräte-ID nur, wenn sie zu genau diesem Platz führt. Bei zwei SIDs mit derselben ID (P14)
// bleibt es für den Platz ohne Modell bei der SID.
bool template_by_device(const Registry &registry, const Node &node);
// Vorschläge für die YAML-Vorlage: Name "ST-630-DE 2FE43D" bzw. "Melder SID 14", Sub-Device-ID
// "melder_2fe43d" bzw. "melder_sid_14".
void template_name(const Node &node, bool by_device, char *out, size_t out_len);
void template_device_key(const Node &node, bool by_device, char *out, size_t out_len);
// "SID 4, 7, 12" für die Knoten aus node_assignable(), deren Bit in assigned fehlt; "keine", wenn alle
// zugeordnet sind. Passt die Liste nicht, endet sie mit "…".
void describe_unassigned(const Registry &registry, uint64_t assigned, char *out, size_t out_len);

}  // namespace wisafe2
}  // namespace esphome
