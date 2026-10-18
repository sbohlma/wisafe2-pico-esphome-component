#pragma once

#include "wisafe2.h"
#include "esphome/components/switch/switch.h"
#include "esphome/core/component.h"
#include "esphome/core/helpers.h"

namespace esphome {
namespace wisafe2 {

class WiSafe2RawLogSwitch : public switch_::Switch, public Component, public Parented<WiSafe2Component> {
 public:
  void setup() override { this->write_state(this->get_initial_state_with_restore_mode().value_or(false)); }
  void dump_config() override { LOG_SWITCH("", "WiSafe2 Rohrahmen loggen", this); }

 protected:
  static constexpr const char *TAG = "wisafe2.switch";

  void write_state(bool state) override {
    this->parent_->set_raw_log(state);
    this->publish_state(state);
  }
};

}  // namespace wisafe2
}  // namespace esphome
