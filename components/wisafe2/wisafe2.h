#pragma once

#include "esphome/components/text_sensor/text_sensor.h"
#include "esphome/core/component.h"

#include <cstddef>
#include <cstdint>

namespace esphome {
namespace wisafe2 {

class WiSafe2Component : public Component {
 public:
  void set_status_sensor(text_sensor::TextSensor *sensor) { this->status_sensor_ = sensor; }

  void setup() override;
  void loop() override;
  void dump_config() override;
  float get_setup_priority() const override { return setup_priority::LATE; }

 protected:
  void emit_frame_(bool complete);

  text_sensor::TextSensor *status_sensor_{nullptr};
  uint8_t frame_[32]{};
  size_t frame_len_{0};
  uint32_t last_byte_ms_{0};
  bool frame_open_{false};
};

}  // namespace wisafe2
}  // namespace esphome
