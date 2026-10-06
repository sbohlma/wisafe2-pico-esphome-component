#pragma once

#include "esphome/core/component.h"
#include "esphome/components/text_sensor/text_sensor.h"

namespace esphome {
namespace wisafe2 {

class WiSafe2Component : public Component {
 public:
  void set_status_sensor(text_sensor::TextSensor *sensor) { this->status_sensor_ = sensor; }
  void setup() override;
  void dump_config() override;
  float get_setup_priority() const override { return setup_priority::LATE; }

 protected:
  text_sensor::TextSensor *status_sensor_{nullptr};
};

}  // namespace wisafe2
}  // namespace esphome
