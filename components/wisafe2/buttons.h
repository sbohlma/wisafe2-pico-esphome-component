#pragma once

#include "core_ipc.h"
#include "esphome/components/button/button.h"

namespace esphome {
namespace wisafe2 {

class WiSafe2InitButton : public button::Button {
 protected:
  void press_action() override { ipc::request_init(); }
};

class WiSafe2SpiButton : public button::Button {
 protected:
  void press_action() override { ipc::request_spi_setup(); }
};

}  // namespace wisafe2
}  // namespace esphome
