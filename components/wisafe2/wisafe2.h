#pragma once

#include "detector.h"
#include "discovery.h"
#include "duplicate_filter.h"
#include "framing.h"
#include "host_identity.h"
#include "messages.h"
#include "registry.h"
#include "scheduler.h"
#include "esphome/components/binary_sensor/binary_sensor.h"
#include "esphome/components/event/event.h"
#include "esphome/components/sensor/sensor.h"
#include "esphome/components/text_sensor/text_sensor.h"
#include "esphome/core/component.h"

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace esphome {
namespace wisafe2 {

class WiSafe2Component : public Component, public Transport {
 public:
  // Geräte-ID in Paket-Reihenfolge und Modell-ID der Brücke (E3).
  void set_identity(uint8_t id0, uint8_t id1, uint8_t id2, uint16_t model);
  // E5: periodische und ereignisgesteuerte Abfragen. Ohne bleibt es bei der Erkundung beim Start und per Button.
  void set_auto_discovery(bool enabled) { this->auto_discovery_ = enabled; }
  void set_remote_status_interval(uint32_t ms) { this->remote_status_interval_ms_ = ms; }
  void set_battery_interval(uint32_t ms) { this->battery_interval_ms_ = ms; }
  void set_remote_timeout(uint32_t ms) { this->scheduler_.set_remote_timeout(ms); }
  void set_status_sensor(text_sensor::TextSensor *sensor) { this->status_sensor_ = sensor; }
  void set_last_reply_sensor(text_sensor::TextSensor *sensor) { this->last_reply_sensor_ = sensor; }
  void set_rm_sid_sensor(sensor::Sensor *sensor) { this->rm_sid_sensor_ = sensor; }
  void set_rm_battery_sensor(sensor::Sensor *sensor) { this->rm_battery_sensor_ = sensor; }
  void set_rm_rssi_sensor(sensor::Sensor *sensor) { this->rm_rssi_sensor_ = sensor; }
  void set_rm_radio_faults_sensor(sensor::Sensor *sensor) { this->rm_radio_faults_sensor_ = sensor; }
  void set_rm_firmware_sensor(text_sensor::TextSensor *sensor) { this->rm_firmware_sensor_ = sensor; }
  void set_rm_device_id_sensor(text_sensor::TextSensor *sensor) { this->rm_device_id_sensor_ = sensor; }
  void set_rm_paired_sensor(binary_sensor::BinarySensor *sensor) { this->rm_paired_sensor_ = sensor; }
  // Stufe 3: Melder aus der YAML und netzweite Entitäten.
  void add_detector(Detector *detector) { this->detectors_.push_back(detector); }
  void set_network_alarm_sensor(binary_sensor::BinarySensor *sensor) { this->network_alarm_sensor_ = sensor; }
  void set_network_silenced_sensor(binary_sensor::BinarySensor *sensor) { this->network_silenced_sensor_ = sensor; }
  void set_discovery_running_sensor(binary_sensor::BinarySensor *sensor) { this->discovery_running_sensor_ = sensor; }
  void set_node_count_sensor(sensor::Sensor *sensor) { this->node_count_sensor_ = sensor; }
  void set_last_event_sensor(text_sensor::TextSensor *sensor) { this->last_event_sensor_ = sensor; }
  void set_unassigned_sensor(text_sensor::TextSensor *sensor) { this->unassigned_sensor_ = sensor; }
  void set_network_event(event::Event *event) { this->network_event_ = event; }

  void setup() override;
  void loop() override;
  void dump_config() override;
  float get_setup_priority() const override { return setup_priority::LATE; }

  bool send_bytes(const uint8_t *bytes, size_t len, uint8_t &id) override;

  // Startet den Init-Ablauf sofort. Richtet vorher den SPI-Slave ein, falls das noch nicht geschehen ist.
  void start_init();
  // D1: Status des eigenen RM.
  void query_rm_status();
  // D3 03: lokale SID-Map.
  void query_sid_map();
  // D3 06 sid 00: Remote-Status, beantwortet vom Ziel-RM mit D4 06.
  void query_remote_status(uint8_t sid);
  // D3 06 sid 01: ID-Anfrage, beantwortet vom Host des Ziels mit C4.
  void query_identity(uint8_t sid);
  // C7 (Batterie), 72 (Fehlerdetails) oder C5 (Fehlerereignisse) an ein Gerät. Fehlen device_id ("") oder
  // model (0), kommen sie aus dem Melderregister.
  void query_device(uint8_t request_code, uint8_t sid, const std::string &device_id, uint16_t model);
  // Button „Discover Network“: Erkundung der SIDs ohne vollständigen Eintrag (Geräte-ID, Klasse, Modell).
  void discover_network();
  // Button „Rediscover Network“: löscht nodes.bin und das Register und erkundet alle SIDs neu.
  void rediscover_network();
  // Button „Netzdetails“: Remote-Status aller erreichbaren SIDs, Batterie und Fehlerdetails der Melder,
  // Fehlerereignisse nur bei gemeldeten Fehlern.
  void query_network_details();
  // Schreibt das Register ins Log, eine Zeile je Knoten.
  void log_registry();
  // Button „Status aktualisieren“ eines Melders: Remote-Status, Batterie und Fehlerdetails dieses Melders.
  void refresh_detector(Detector *detector);
  // Schreibt eine YAML-Vorlage für alle Melder ohne Eintrag in detectors ins Log, verteilt über mehrere
  // Logzeilen.
  void log_yaml_template();

 protected:
  enum class StartState : uint8_t { WaitNetwork, Settle, Running };
  enum RequestTag : uint8_t {
    TAG_INIT = 1,
    TAG_RM_STATUS,
    TAG_SID_MAP,
    TAG_IDENTITY_REPLY,
    TAG_HOST_REPLY,
    TAG_QUERY_REMOTE_STATUS,
    TAG_QUERY_IDENTITY,
    TAG_QUERY_DEVICE,
    TAG_DISCOVERY,
  };

  void start_module_();
  void enqueue_init_();
  void query_after_init_();
  bool enqueue_(const Request &request);
  void handle_frame_(const Frame &frame, uint32_t now);
  void handle_local_(const LocalMessage &msg, uint32_t now);
  void handle_radio_(const RadioMessage &msg, uint32_t now);
  void handle_rm_status_(const LocalMessage &msg);
  void handle_completion_(const Completion &completion);
  void report_query_(const Completion &completion);
  void publish_last_reply_(const char *text);
  void publish_status_(const char *status);
  void update_module_status_();

  Request remote_status_request_(uint8_t tag, uint8_t sid) const;
  Request identity_request_(uint8_t tag, uint8_t sid) const;
  Request device_request_(uint8_t tag, uint8_t request_code, const uint8_t *id, uint16_t model, uint8_t sid) const;

  void note_changes_(uint8_t sid, uint8_t changes);
  void want_unknown_(uint64_t map);
  void start_discovery_(uint64_t map);
  bool discovery_ready_() const;
  void service_discovery_(uint32_t now);
  void enqueue_discovery_(const DiscoveryTask &task);
  void load_registry_();
  void mark_dirty_();
  void service_store_(uint32_t now);

  void assign_detectors_();
  void publish_entities_(uint32_t now);
  void report_event_(const RadioMessage &msg);
  bool next_template_node_(const Node *&node);
  void service_template_(uint32_t now);

  Identity identity_{};
  bool auto_discovery_{false};
  uint32_t remote_status_interval_ms_{0};
  uint32_t battery_interval_ms_{0};

  text_sensor::TextSensor *status_sensor_{nullptr};
  text_sensor::TextSensor *last_reply_sensor_{nullptr};
  sensor::Sensor *rm_sid_sensor_{nullptr};
  sensor::Sensor *rm_battery_sensor_{nullptr};
  sensor::Sensor *rm_rssi_sensor_{nullptr};
  sensor::Sensor *rm_radio_faults_sensor_{nullptr};
  text_sensor::TextSensor *rm_firmware_sensor_{nullptr};
  text_sensor::TextSensor *rm_device_id_sensor_{nullptr};
  binary_sensor::BinarySensor *rm_paired_sensor_{nullptr};
  const char *status_{nullptr};

  std::vector<Detector *> detectors_;
  binary_sensor::BinarySensor *network_alarm_sensor_{nullptr};
  binary_sensor::BinarySensor *network_silenced_sensor_{nullptr};
  binary_sensor::BinarySensor *discovery_running_sensor_{nullptr};
  sensor::Sensor *node_count_sensor_{nullptr};
  text_sensor::TextSensor *last_event_sensor_{nullptr};
  text_sensor::TextSensor *unassigned_sensor_{nullptr};
  event::Event *network_event_{nullptr};
  // Zuordnung der Melder zu den Plätzen im Register, neu berechnet nach jeder Änderung einer Kennung.
  bool assign_needed_{true};
  uint64_t assigned_{0};
  // Veröffentlicht wird gesammelt in loop(): SIDs mit Änderungen oder alles.
  uint64_t dirty_sids_{0};
  bool publish_all_{true};
  bool map_seen_{false};
  enum class TemplatePhase : uint8_t { Idle, Devices, Detectors };
  TemplatePhase template_phase_{TemplatePhase::Idle};
  uint8_t template_sid_{0};
  uint32_t template_last_ms_{0};

  FrameDecoder decoder_;
  Scheduler scheduler_{*this};
  DuplicateFilter duplicates_;
  ReplyLimiter reply_limiter_;
  Registry registry_;
  Discovery discovery_;
  uint8_t next_command_id_{0};
  uint8_t own_sid_{NO_SID};
  uint32_t last_frame_ms_{0};

  bool store_ok_{false};
  const char *store_state_{"nicht geladen"};
  bool store_dirty_{false};
  uint32_t store_dirty_since_ms_{0};
  bool startup_discovery_requested_{false};
  uint32_t discovery_started_ms_{0};

  bool module_started_{false};
  StartState start_state_{StartState::WaitNetwork};
  uint32_t settle_since_ms_{0};
  bool init_pending_{false};
  int init_attempt_{0};
  bool await_init_report_{false};
  uint32_t init_report_deadline_ms_{0};

  bool paired_{true};
  bool id_mismatch_{false};
  bool id_confirmed_{false};
};

}  // namespace wisafe2
}  // namespace esphome
