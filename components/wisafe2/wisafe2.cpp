#include "wisafe2.h"

#include "core_ipc.h"
#include "frame_analyzer.h"
#include "esphome/core/hal.h"
#include "esphome/core/log.h"

namespace esphome {
namespace wisafe2 {

static const char *const TAG = "wisafe2";
static constexpr uint32_t FRAME_GAP_MS = 80;

void WiSafe2Component::emit_frame_(bool complete) {
  if (this->frame_len_ == 0) {
    this->frame_open_ = false;
    return;
  }
  char text[192];
  describe_frame(this->frame_, this->frame_len_, complete, text, sizeof(text));
  ESP_LOGI(TAG, "%s", text);
  this->frame_len_ = 0;
  this->frame_open_ = false;
}

void WiSafe2Component::setup() {
  ESP_LOGI(TAG, "SPI-Slave auf Kern 1, nur Empfang");
  if (this->status_sensor_ != nullptr) {
    this->status_sensor_->publish_state("bereit");
  }
}

void WiSafe2Component::loop() {
  char text[ipc::LogMessage::TEXT_LEN];
  while (ipc::poll_core1_log(text, sizeof(text))) {
    ESP_LOGI(TAG, "%s", text);
  }
  const uint32_t dropped = ipc::take_dropped_logs();
  if (dropped != 0) {
    ESP_LOGW(TAG, "Kern 1: %u Logzeilen verworfen", dropped);
  }

  uint8_t byte = 0;
  while (ipc::poll_spi_byte(byte)) {
    if (this->frame_len_ >= sizeof(this->frame_)) {
      this->emit_frame_(false);
    }
    this->frame_[this->frame_len_++] = byte;
    this->last_byte_ms_ = millis();
    this->frame_open_ = true;
    if (byte == 0x7E) {
      this->emit_frame_(true);
    }
  }

  if (this->frame_open_ && static_cast<int32_t>(millis() - this->last_byte_ms_) >= static_cast<int32_t>(FRAME_GAP_MS)) {
    this->emit_frame_(false);
  }
}

void WiSafe2Component::dump_config() {
  ESP_LOGCONFIG(TAG, "WiSafe2:");
  LOG_TEXT_SENSOR("  ", "Status", this->status_sensor_);
  ESP_LOGCONFIG(TAG, "  SPI-Slave Kern 1: GP16 MOSI, GP17 CS, GP18 SCK, GP19 MISO, GP20 IRQ");
  ESP_LOGCONFIG(TAG, "  Empfang mit IRQ-Quittung 8 us, keine Befehle");
}

}  // namespace wisafe2
}  // namespace esphome
