#include "detector.h"

#include "messages.h"
#include "esphome/core/log.h"

#include <cmath>
#include <cstdio>

namespace esphome {
namespace wisafe2 {

static const char *const TAG = "wisafe2";

// „zuletzt gesehen“ folgt jeder Nachricht höchstens in diesem Abstand, damit Home Assistant nicht bei jeder Kopie
// eines Rahmens einen neuen Zustand speichert.
static constexpr uint32_t LAST_SEEN_STEP_MS = 60000;

void publish_tri(binary_sensor::BinarySensor *sensor, Tri state) {
  if (sensor == nullptr) {
    return;
  }
  if (state == Tri::Unknown) {
    sensor->invalidate_state();
  } else {
    sensor->publish_state(state == Tri::On);
  }
}

void publish_number(sensor::Sensor *sensor, bool known, float value) {
  if (sensor == nullptr) {
    return;
  }
  if (!known) {
    if (sensor->has_state() && !std::isnan(sensor->state)) {
      sensor->publish_state(NAN);
    }
    return;
  }
  if (!sensor->has_state() || sensor->state != value) {
    sensor->publish_state(value);
  }
}

void publish_text(text_sensor::TextSensor *sensor, const char *text) {
  if (sensor != nullptr && (!sensor->has_state() || sensor->state != text)) {
    sensor->publish_state(text);
  }
}

void Detector::set_device_id(uint8_t id0, uint8_t id1, uint8_t id2) {
  this->has_device_ = true;
  this->device_id_[0] = id0;
  this->device_id_[1] = id1;
  this->device_id_[2] = id2;
}

const Node *Detector::assign(const Registry &registry) {
  const Node *node = registry.resolve(this->sid_, this->device_id());
  const uint8_t sid = node != nullptr ? node->sid : NO_SID;
  if (sid != this->resolved_sid_) {
    this->resolved_sid_ = sid;
    this->seen_published_ = false;
    this->test_published_ = false;
  }
  return node;
}

void Detector::publish(const Node *node, bool map_seen, uint32_t now_ms, int64_t epoch) {
  if (map_seen && this->online_ != nullptr) {
    this->online_->publish_state(node != nullptr && node_online(*node));
  }
  publish_tri(this->alarm_, node != nullptr ? node_alarm(*node) : Tri::Unknown);
  publish_tri(this->fault_, node != nullptr ? node_fault(*node) : Tri::Unknown);
  publish_tri(this->socket_problem_, node != nullptr ? node_socket_problem(*node) : Tri::Unknown);
  publish_tri(this->battery_low_, node != nullptr ? node_battery_low(*node) : Tri::Unknown);
  publish_tri(this->rm_battery_low_, node != nullptr ? node_rm_battery_low(*node) : Tri::Unknown);

  publish_number(this->battery_voltage_, node != nullptr && node->has_battery && node->has_battery_report(),
                 node != nullptr ? static_cast<float>(node->battery_mv) : 0.0f);
  const bool has_rm = node != nullptr && node->has_rm;
  publish_number(this->rm_battery_voltage_, has_rm,
                 has_rm ? static_cast<float>(rm_battery_mv(node->rm_bvnl)) / 1000.0f : 0.0f);
  publish_number(this->rssi_, has_rm, has_rm ? static_cast<float>(node->rm_rssi) : 0.0f);

  if (node == nullptr) {
    return;
  }
  char text[160];
  if (this->model_ != nullptr && (node->has_model() || node->has_device())) {
    describe_model(*node, text, sizeof(text));
    publish_text(this->model_, text);
  }
  if (this->fault_details_ != nullptr && node->has_fault_details) {
    describe_fault_details(node->model, node->fault_details, text, sizeof(text));
    publish_text(this->fault_details_, text);
  }
  if (this->last_test_ != nullptr && node->has_test &&
      (!this->test_published_ || node->test_ms != this->test_published_ms_)) {
    const int64_t at = epoch_valid(epoch) ? epoch - static_cast<int64_t>((now_ms - node->test_ms) / 1000u) : 0;
    describe_test(node->test_result, at, text, sizeof(text));
    publish_text(this->last_test_, text);
    this->test_published_ = true;
    this->test_published_ms_ = node->test_ms;
  }
  if (this->last_seen_ != nullptr && node->seen && epoch_valid(epoch) &&
      (!this->seen_published_ || node->last_seen_ms - this->seen_published_ms_ >= LAST_SEEN_STEP_MS)) {
    format_timestamp(epoch - static_cast<int64_t>((now_ms - node->last_seen_ms) / 1000u), text, sizeof(text));
    publish_text(this->last_seen_, text);
    this->seen_published_ = true;
    this->seen_published_ms_ = node->last_seen_ms;
  }
}

void Detector::trigger(const char *event_type) {
  if (this->event_ != nullptr) {
    this->event_->trigger(event_type);
  }
}

void Detector::dump_config(const char *prefix) const {
  char id[8] = "-";
  if (this->has_device_) {
    format_device_id(this->device_id_, id, sizeof(id));
  }
  char sid[4] = "-";
  if (this->sid_ <= MAX_SID) {
    std::snprintf(sid, sizeof(sid), "%u", this->sid_);
  }
  ESP_LOGCONFIG(TAG, "%sMelder \"%s\": SID %s, Geräte-ID %s, zugeordnet: %s", prefix, this->name_, sid, id,
                this->resolved_sid_ <= MAX_SID ? "ja" : "nein");
}

}  // namespace wisafe2
}  // namespace esphome
