#include "wisafe2.h"

#include "core_ipc.h"
#include "frame_analyzer.h"
#include "esphome/components/network/util.h"
#include "esphome/core/hal.h"
#include "esphome/core/log.h"

namespace esphome {
namespace wisafe2 {

static const char *const TAG = "wisafe2";
static constexpr uint32_t FRAME_GAP_MS = 80;

static constexpr uint8_t INIT_COMMAND[] = {0xD3, 0x19, 0x50, 0x00, 0x7E};
// Gibt Log-Clients nach dem WLAN-Aufbau Zeit, sich zu verbinden.
static constexpr uint32_t MODULE_START_DELAY_MS = 5000;
static constexpr uint32_t INIT_SENT_TIMEOUT_MS = 1000;
static constexpr uint32_t INIT_REPLY_MS = 125;
static constexpr uint32_t INIT_PAUSE_MS = 500;
static constexpr int INIT_ATTEMPTS = 50;

void WiSafe2Component::emit_frame_(bool complete) {
  if (this->frame_len_ == 0) {
    this->frame_open_ = false;
    return;
  }
  if (complete && this->frame_len_ == 2 && this->frame_[0] == 0x46 &&
      (this->init_state_ == InitState::AwaitSent || this->init_state_ == InitState::AwaitReply ||
       this->init_state_ == InitState::Pause)) {
    this->init_state_ = InitState::Done;
  }
  char text[192];
  describe_frame(this->frame_, this->frame_len_, complete, text, sizeof(text));
  ESP_LOGI(TAG, "%s", text);
  this->frame_len_ = 0;
  this->frame_open_ = false;
}

bool WiSafe2Component::send_command(const uint8_t *bytes, size_t len, uint8_t *id) {
  const uint8_t command_id = ++this->next_command_id_;
  if (!ipc::send_command(command_id, bytes, len)) {
    return false;
  }
  if (id != nullptr) {
    *id = command_id;
  }
  return true;
}

void WiSafe2Component::start_module_() {
  if (!this->module_started_) {
    ipc::request_spi_setup();
    this->module_started_ = true;
  }
}

void WiSafe2Component::start_init() {
  this->start_module_();
  this->init_attempt_ = 0;
  if (this->init_state_ != InitState::AwaitSent) {
    this->init_state_ = InitState::Send;
  }
}

void WiSafe2Component::handle_command_result_(uint8_t id, bool ok) {
  if (this->init_state_ == InitState::AwaitSent && id == this->init_command_id_) {
    this->init_state_ = ok ? InitState::AwaitReply : InitState::Pause;
    this->init_mark_ms_ = millis();
  }
}

void WiSafe2Component::service_init_() {
  const uint32_t now = millis();
  switch (this->init_state_) {
    case InitState::WaitNetwork:
      if (network::is_connected()) {
        this->init_state_ = InitState::Settle;
        this->init_mark_ms_ = now;
      }
      break;
    case InitState::Settle:
      if (now - this->init_mark_ms_ >= MODULE_START_DELAY_MS) {
        this->start_module_();
        this->init_state_ = InitState::Send;
      }
      break;
    case InitState::Send:
      if (this->send_command(INIT_COMMAND, sizeof(INIT_COMMAND), &this->init_command_id_)) {
        this->init_state_ = InitState::AwaitSent;
        this->init_mark_ms_ = now;
      }
      break;
    case InitState::AwaitSent:
      if (now - this->init_mark_ms_ >= INIT_SENT_TIMEOUT_MS) {
        this->init_state_ = InitState::Pause;
        this->init_mark_ms_ = now;
      }
      break;
    case InitState::AwaitReply:
      if (now - this->init_mark_ms_ >= INIT_REPLY_MS) {
        this->init_state_ = InitState::Pause;
        this->init_mark_ms_ = now;
      }
      break;
    case InitState::Pause:
      if (now - this->init_mark_ms_ >= INIT_PAUSE_MS) {
        this->init_state_ = ++this->init_attempt_ >= INIT_ATTEMPTS ? InitState::Done : InitState::Send;
      }
      break;
    case InitState::Done:
      break;
  }
}

void WiSafe2Component::setup() {
  ESP_LOGI(TAG, "Modulstart nach WLAN-Verbindung");
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

  ipc::CommandResult result{};
  while (ipc::poll_command_result(result)) {
    this->handle_command_result_(result.id, result.ok);
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

  this->service_init_();
}

void WiSafe2Component::dump_config() {
  ESP_LOGCONFIG(TAG, "WiSafe2:");
  LOG_TEXT_SENSOR("  ", "Status", this->status_sensor_);
  ESP_LOGCONFIG(TAG, "  SPI-Slave Kern 1: GP16 MOSI, GP17 CS, GP18 SCK, GP19 MISO, GP20 IRQ");
  ESP_LOGCONFIG(TAG, "  Empfang mit IRQ-Quittung 8 us, Befehle von Kern 0, Init D3 19 50 00 7E");
}

}  // namespace wisafe2
}  // namespace esphome
