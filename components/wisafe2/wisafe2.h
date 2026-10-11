#pragma once

#include "framing.h"
#include "scheduler.h"
#include "esphome/components/text_sensor/text_sensor.h"
#include "esphome/core/component.h"

#include <cstddef>
#include <cstdint>

namespace esphome {
namespace wisafe2 {

class WiSafe2Component : public Component, public Transport {
 public:
  void set_status_sensor(text_sensor::TextSensor *sensor) { this->status_sensor_ = sensor; }

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

 protected:
  enum class StartState : uint8_t { WaitNetwork, Settle, Running };
  enum RequestTag : uint8_t { TAG_INIT = 1, TAG_RM_STATUS, TAG_SID_MAP };

  void start_module_();
  void enqueue_init_();
  void enqueue_(const Request &request);
  void handle_frame_(const Frame &frame, uint32_t now);
  void handle_completion_(const Completion &completion);
  void publish_status_(const char *status);

  text_sensor::TextSensor *status_sensor_{nullptr};
  const char *status_{nullptr};

  FrameDecoder decoder_;
  Scheduler scheduler_{*this};
  uint8_t next_command_id_{0};

  bool module_started_{false};
  StartState start_state_{StartState::WaitNetwork};
  uint32_t settle_since_ms_{0};
  bool init_pending_{false};
  int init_attempt_{0};
};

}  // namespace wisafe2
}  // namespace esphome
