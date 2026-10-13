#pragma once

#include "node_view.h"
#include "registry.h"
#include "esphome/components/binary_sensor/binary_sensor.h"
#include "esphome/components/event/event.h"
#include "esphome/components/sensor/sensor.h"
#include "esphome/components/text_sensor/text_sensor.h"

#include <cstdint>

namespace esphome {
namespace wisafe2 {

// Ein Melder aus der YAML (Stufe 3) mit seinen Entitäten. Er wird über die Geräte-ID oder die SID einem Platz im
// Register zugeordnet (Registry::resolve) und veröffentlicht nur geänderte Werte.
class Detector {
 public:
  Detector(const char *name, uint8_t sid) : name_(name), sid_(sid) {}
  // Geräte-ID in Paket-Reihenfolge.
  void set_device_id(uint8_t id0, uint8_t id1, uint8_t id2);

  void set_alarm_sensor(binary_sensor::BinarySensor *sensor) { this->alarm_ = sensor; }
  void set_fault_sensor(binary_sensor::BinarySensor *sensor) { this->fault_ = sensor; }
  void set_socket_problem_sensor(binary_sensor::BinarySensor *sensor) { this->socket_problem_ = sensor; }
  void set_battery_low_sensor(binary_sensor::BinarySensor *sensor) { this->battery_low_ = sensor; }
  void set_rm_battery_low_sensor(binary_sensor::BinarySensor *sensor) { this->rm_battery_low_ = sensor; }
  void set_online_sensor(binary_sensor::BinarySensor *sensor) { this->online_ = sensor; }
  void set_battery_voltage_sensor(sensor::Sensor *sensor) { this->battery_voltage_ = sensor; }
  void set_rm_battery_voltage_sensor(sensor::Sensor *sensor) { this->rm_battery_voltage_ = sensor; }
  void set_rssi_sensor(sensor::Sensor *sensor) { this->rssi_ = sensor; }
  void set_last_seen_sensor(text_sensor::TextSensor *sensor) { this->last_seen_ = sensor; }
  void set_model_sensor(text_sensor::TextSensor *sensor) { this->model_ = sensor; }
  void set_fault_details_sensor(text_sensor::TextSensor *sensor) { this->fault_details_ = sensor; }
  void set_last_test_sensor(text_sensor::TextSensor *sensor) { this->last_test_ = sensor; }
  void set_event(event::Event *event) { this->event_ = event; }

  const char *name() const { return this->name_; }
  uint8_t configured_sid() const { return this->sid_; }
  const uint8_t *device_id() const { return this->has_device_ ? this->device_id_ : nullptr; }
  // SID des zugeordneten Platzes, NO_SID ohne Zuordnung. Gilt bis zum nächsten assign().
  uint8_t sid() const { return this->resolved_sid_; }
  const Node *assign(const Registry &registry);

  // map_seen: Erst nach der ersten SID-Map ist „im Netz“ bekannt. epoch: aktuelle Systemzeit, vor 2020 ungestellt.
  void publish(const Node *node, bool map_seen, uint32_t now_ms, int64_t epoch);
  void trigger(const char *event_type);
  void dump_config(const char *prefix) const;

 protected:
  const char *name_;
  uint8_t sid_;
  bool has_device_{false};
  uint8_t device_id_[3]{};
  uint8_t resolved_sid_{NO_SID};

  // Letzter veröffentlichter Zeitpunkt in Node::last_seen_ms bzw. Node::test_ms.
  bool seen_published_{false};
  uint32_t seen_published_ms_{0};
  bool test_published_{false};
  uint32_t test_published_ms_{0};

  binary_sensor::BinarySensor *alarm_{nullptr};
  binary_sensor::BinarySensor *fault_{nullptr};
  binary_sensor::BinarySensor *socket_problem_{nullptr};
  binary_sensor::BinarySensor *battery_low_{nullptr};
  binary_sensor::BinarySensor *rm_battery_low_{nullptr};
  binary_sensor::BinarySensor *online_{nullptr};
  sensor::Sensor *battery_voltage_{nullptr};
  sensor::Sensor *rm_battery_voltage_{nullptr};
  sensor::Sensor *rssi_{nullptr};
  text_sensor::TextSensor *last_seen_{nullptr};
  text_sensor::TextSensor *model_{nullptr};
  text_sensor::TextSensor *fault_details_{nullptr};
  text_sensor::TextSensor *last_test_{nullptr};
  event::Event *event_{nullptr};
};

// Hilfen für die Entitäten, auch für die netzweiten in WiSafe2Component.
void publish_tri(binary_sensor::BinarySensor *sensor, Tri state);
void publish_number(sensor::Sensor *sensor, bool known, float value);
void publish_text(text_sensor::TextSensor *sensor, const char *text);

}  // namespace wisafe2
}  // namespace esphome
