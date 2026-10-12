#pragma once

#include "core_ipc.h"
#include "wisafe2.h"
#include "esphome/components/button/button.h"
#include "esphome/core/helpers.h"

namespace esphome {
namespace wisafe2 {

class WiSafe2InitButton : public button::Button, public Parented<WiSafe2Component> {
 protected:
  void press_action() override { this->parent_->start_init(); }
};

class WiSafe2SpiButton : public button::Button {
 protected:
  void press_action() override { ipc::request_spi_setup(); }
};

class WiSafe2RmStatusButton : public button::Button, public Parented<WiSafe2Component> {
 protected:
  void press_action() override { this->parent_->query_rm_status(); }
};

class WiSafe2SidMapButton : public button::Button, public Parented<WiSafe2Component> {
 protected:
  void press_action() override { this->parent_->query_sid_map(); }
};

class WiSafe2DiscoverButton : public button::Button, public Parented<WiSafe2Component> {
 protected:
  void press_action() override { this->parent_->discover_network(); }
};

class WiSafe2RediscoverButton : public button::Button, public Parented<WiSafe2Component> {
 protected:
  void press_action() override { this->parent_->rediscover_network(); }
};

class WiSafe2DetailsButton : public button::Button, public Parented<WiSafe2Component> {
 protected:
  void press_action() override { this->parent_->query_network_details(); }
};

class WiSafe2RegistryLogButton : public button::Button, public Parented<WiSafe2Component> {
 protected:
  void press_action() override { this->parent_->log_registry(); }
};

}  // namespace wisafe2
}  // namespace esphome
