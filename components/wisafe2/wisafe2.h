#pragma once

#include "duplicate_filter.h"
#include "framing.h"
#include "host_identity.h"
#include "messages.h"
#include "node_cache.h"
#include "scheduler.h"
#include "esphome/components/binary_sensor/binary_sensor.h"
#include "esphome/components/sensor/sensor.h"
#include "esphome/components/text_sensor/text_sensor.h"
#include "esphome/core/component.h"

#include <cstddef>
#include <cstdint>
#include <string>

namespace esphome {
namespace wisafe2 {

class WiSafe2Component : public Component, public Transport {
 public:
  // Geräte-ID in Paket-Reihenfolge und Modell-ID der Brücke (E3).
  void set_identity(uint8_t id0, uint8_t id1, uint8_t id2, uint16_t model);
  void set_status_sensor(text_sensor::TextSensor *sensor) { this->status_sensor_ = sensor; }
  void set_last_reply_sensor(text_sensor::TextSensor *sensor) { this->last_reply_sensor_ = sensor; }
  void set_rm_sid_sensor(sensor::Sensor *sensor) { this->rm_sid_sensor_ = sensor; }
  void set_rm_battery_sensor(sensor::Sensor *sensor) { this->rm_battery_sensor_ = sensor; }
  void set_rm_rssi_sensor(sensor::Sensor *sensor) { this->rm_rssi_sensor_ = sensor; }
  void set_rm_radio_faults_sensor(sensor::Sensor *sensor) { this->rm_radio_faults_sensor_ = sensor; }
  void set_rm_firmware_sensor(text_sensor::TextSensor *sensor) { this->rm_firmware_sensor_ = sensor; }
  void set_rm_device_id_sensor(text_sensor::TextSensor *sensor) { this->rm_device_id_sensor_ = sensor; }
  void set_rm_paired_sensor(binary_sensor::BinarySensor *sensor) { this->rm_paired_sensor_ = sensor; }

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
  // model (0), kommen sie aus den bisher gehörten Nachrichten der SID.
  void query_device(uint8_t request_code, uint8_t sid, const std::string &device_id, uint16_t model);

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
  };

  void start_module_();
  void enqueue_init_();
  void query_after_init_();
  bool enqueue_(const Request &request);
  void handle_frame_(const Frame &frame, uint32_t now);
  void handle_local_(const LocalMessage &msg);
  void handle_radio_(const RadioMessage &msg, uint32_t now);
  void handle_rm_status_(const LocalMessage &msg);
  void handle_completion_(const Completion &completion);
  void report_query_(const Completion &completion);
  void publish_last_reply_(const char *text);
  void publish_status_(const char *status);
  void update_module_status_();

  Identity identity_{};

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

  FrameDecoder decoder_;
  Scheduler scheduler_{*this};
  DuplicateFilter duplicates_;
  ReplyLimiter reply_limiter_;
  NodeCache nodes_;
  uint8_t next_command_id_{0};

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
