#pragma once

#include "wisafe2.h"
#include "esphome/core/automation.h"
#include "esphome/core/helpers.h"

#include <string>

namespace esphome {
namespace wisafe2 {

template<typename... Ts> class QueryRemoteStatusAction : public Action<Ts...>, public Parented<WiSafe2Component> {
 public:
  TEMPLATABLE_VALUE(uint8_t, sid)

  void play(const Ts &...x) override { this->parent_->query_remote_status(this->sid_.value(x...)); }
};

template<typename... Ts> class QueryIdentityAction : public Action<Ts...>, public Parented<WiSafe2Component> {
 public:
  TEMPLATABLE_VALUE(uint8_t, sid)

  void play(const Ts &...x) override { this->parent_->query_identity(this->sid_.value(x...)); }
};

// C7, 72 oder C5 an ein Gerät; ohne device_id und model kommen beide aus den gehörten Nachrichten der SID.
template<typename... Ts> class QueryDeviceAction : public Action<Ts...>, public Parented<WiSafe2Component> {
 public:
  explicit QueryDeviceAction(uint8_t request_code) : request_code_(request_code) {}

  TEMPLATABLE_VALUE(uint8_t, sid)
  TEMPLATABLE_VALUE(std::string, device_id)
  TEMPLATABLE_VALUE(uint16_t, model)

  void play(const Ts &...x) override {
    this->parent_->query_device(this->request_code_, this->sid_.value_or(x..., NO_SID),
                                this->device_id_.value_or(x..., std::string()), this->model_.value_or(x..., 0));
  }

 protected:
  uint8_t request_code_;
};

}  // namespace wisafe2
}  // namespace esphome
