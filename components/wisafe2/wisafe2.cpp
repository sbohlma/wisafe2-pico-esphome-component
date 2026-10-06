#include "wisafe2.h"
#include "esphome/core/log.h"

namespace esphome {
namespace wisafe2 {

static const char *const TAG = "wisafe2";

void WiSafe2Component::setup() {
  ESP_LOGI(TAG, "Komponente geladen, SPI ist noch nicht aktiv");
  if (this->status_sensor_ != nullptr) {
    this->status_sensor_->publish_state("bereit");
  }
}

void WiSafe2Component::dump_config() {
  ESP_LOGCONFIG(TAG, "WiSafe2:");
  LOG_TEXT_SENSOR("  ", "Status", this->status_sensor_);
}

}  // namespace wisafe2
}  // namespace esphome
