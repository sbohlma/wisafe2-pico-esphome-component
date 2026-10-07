#pragma once

#include "esphome/components/switch/switch.h"
#include "esphome/components/text_sensor/text_sensor.h"
#include "esphome/core/component.h"

namespace esphome {
namespace wisafe2 {

class WiSafe2Component;

class WiSafe2BlinkSwitch : public switch_::Switch, public Component {
 public:
  void set_parent(WiSafe2Component *parent) { this->parent_ = parent; }
  void setup() override;
  float get_setup_priority() const override { return setup_priority::LATE; }

 protected:
  void write_state(bool state) override;

  WiSafe2Component *parent_{nullptr};
};

class WiSafe2Component : public Component {
 public:
  void set_status_sensor(text_sensor::TextSensor *sensor) { this->status_sensor_ = sensor; }
  void set_blink_switch(WiSafe2BlinkSwitch *blink_switch) {
    this->blink_switch_ = blink_switch;
    blink_switch->set_parent(this);
  }
  void send_blink_enabled(bool enabled);

  void setup() override;
  void loop() override;
  void dump_config() override;
  float get_setup_priority() const override { return setup_priority::LATE; }

 protected:
  text_sensor::TextSensor *status_sensor_{nullptr};
  WiSafe2BlinkSwitch *blink_switch_{nullptr};
};

}  // namespace wisafe2
}  // namespace esphome
