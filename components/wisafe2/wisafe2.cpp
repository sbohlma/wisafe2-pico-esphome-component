#include "wisafe2.h"

#include "core_ipc.h"
#include "frame_analyzer.h"
#include "messages.h"
#include "esphome/components/network/util.h"
#include "esphome/core/hal.h"
#include "esphome/core/log.h"

#include <cstdio>

namespace esphome {
namespace wisafe2 {

static const char *const TAG = "wisafe2";

static constexpr uint8_t INIT_PAYLOAD[] = {0xD3, 0x19, 0x50, 0x00};
static constexpr uint8_t RM_STATUS_PAYLOAD[] = {0xD1};
static constexpr uint8_t RM_STATUS_REPLY[] = {MSG_RM_STATUS};
static constexpr uint8_t SID_MAP_PAYLOAD[] = {0xD3, 0x03};
static constexpr uint8_t SID_MAP_REPLY[] = {MSG_RM_REPORT, 0x03};
// Gibt Log-Clients nach dem WLAN-Aufbau Zeit, sich zu verbinden.
static constexpr uint32_t MODULE_START_DELAY_MS = 5000;
static constexpr int INIT_ATTEMPTS = 50;

static const char *const STATUS_WAIT_NETWORK = "warte auf WLAN";
static const char *const STATUS_INIT = "Init läuft";
static const char *const STATUS_READY = "Modul bereit";
static const char *const STATUS_NO_REPLY = "keine Antwort";
static const char *const STATUS_UNPAIRED = "ungekoppelt";

void WiSafe2Component::publish_status_(const char *status) {
  if (this->status_ == status) {
    return;
  }
  this->status_ = status;
  ESP_LOGI(TAG, "Status: %s", status);
  if (this->status_sensor_ != nullptr) {
    this->status_sensor_->publish_state(status);
  }
}

bool WiSafe2Component::send_bytes(const uint8_t *bytes, size_t len, uint8_t &id) {
  const uint8_t command_id = ++this->next_command_id_;
  if (!ipc::send_command(command_id, bytes, len)) {
    return false;
  }
  id = command_id;
  char text[ipc::Command::MAX_LEN * 3 + 1];
  size_t used = 0;
  for (size_t index = 0; index < len && used + 4 <= sizeof(text); ++index) {
    used += std::snprintf(text + used, sizeof(text) - used, index == 0 ? "%02X" : " %02X", bytes[index]);
  }
  text[used] = '\0';
  ESP_LOGD(TAG, "Sende %s", text);
  return true;
}

void WiSafe2Component::start_module_() {
  if (!this->module_started_) {
    ipc::request_spi_setup();
    this->module_started_ = true;
  }
  this->start_state_ = StartState::Running;
}

void WiSafe2Component::enqueue_(const Request &request) {
  if (!this->scheduler_.enqueue(request)) {
    ESP_LOGW(TAG, "Warteschlange voll, Befehl %02X verworfen", request.payload[0]);
  }
}

void WiSafe2Component::enqueue_init_() {
  this->init_pending_ = true;
  this->init_attempt_++;
  this->publish_status_(STATUS_INIT);
  this->enqueue_(Request::remote(TAG_INIT, INIT_PAYLOAD, sizeof(INIT_PAYLOAD)));
}

void WiSafe2Component::start_init() {
  this->start_module_();
  this->init_attempt_ = 0;
  if (!this->init_pending_) {
    this->enqueue_init_();
  }
}

void WiSafe2Component::query_rm_status() {
  this->start_module_();
  this->enqueue_(
      Request::local(TAG_RM_STATUS, RM_STATUS_PAYLOAD, sizeof(RM_STATUS_PAYLOAD), RM_STATUS_REPLY, sizeof(RM_STATUS_REPLY)));
}

void WiSafe2Component::query_sid_map() {
  this->start_module_();
  this->enqueue_(Request::local(TAG_SID_MAP, SID_MAP_PAYLOAD, sizeof(SID_MAP_PAYLOAD), SID_MAP_REPLY, sizeof(SID_MAP_REPLY)));
}

void WiSafe2Component::handle_completion_(const Completion &completion) {
  switch (completion.request.tag) {
    case TAG_INIT:
      this->init_pending_ = false;
      if (completion.outcome == Outcome::Ok) {
        ESP_LOGI(TAG, "Init quittiert nach %d Versuch(en)", this->init_attempt_);
        this->publish_status_(STATUS_READY);
        this->query_rm_status();
        this->query_sid_map();
      } else if (this->init_attempt_ < INIT_ATTEMPTS) {
        ESP_LOGD(TAG, "Init %s, Versuch %d/%d", outcome_name(completion.outcome), this->init_attempt_,
                 INIT_ATTEMPTS);
        this->enqueue_init_();
      } else {
        ESP_LOGW(TAG, "Init nach %d Versuchen ohne Quittung", INIT_ATTEMPTS);
        this->publish_status_(STATUS_NO_REPLY);
      }
      break;
    case TAG_RM_STATUS:
      if (completion.outcome != Outcome::Ok) {
        ESP_LOGW(TAG, "D1 (RM-Status): %s nach %u Sendeversuch(en)", outcome_name(completion.outcome),
                 static_cast<unsigned>(completion.request.send_attempts));
      }
      break;
    case TAG_SID_MAP:
      if (completion.outcome != Outcome::Ok) {
        ESP_LOGW(TAG, "D3 03 (SID-Map): %s nach %u Sendeversuch(en)", outcome_name(completion.outcome),
                 static_cast<unsigned>(completion.request.send_attempts));
      }
      break;
    default:
      break;
  }
}

void WiSafe2Component::handle_frame_(const Frame &frame, uint32_t now) {
  char text[256];
  describe_frame(frame, text, sizeof(text));
  ESP_LOGI(TAG, "%s", text);

  // D2 kommt auch unaufgefordert etwa stündlich.
  LocalMessage msg;
  if (parse_local(frame, msg)) {
    if (msg.type == LocalType::RmStatus) {
      this->publish_status_(msg.sid == SID_UNPAIRED ? STATUS_UNPAIRED : STATUS_READY);
    } else if (msg.type == LocalType::LocalSidMap) {
      this->publish_status_(msg.map == 0 ? STATUS_UNPAIRED : STATUS_READY);
    }
  }

  this->scheduler_.on_frame(frame, now);
}

void WiSafe2Component::setup() {
  this->scheduler_.set_handler([this](const Completion &completion) { this->handle_completion_(completion); });
  ESP_LOGI(TAG, "Modulstart nach WLAN-Verbindung");
  this->publish_status_(STATUS_WAIT_NETWORK);
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

  uint32_t now = millis();
  ipc::CommandResult result{};
  while (ipc::poll_command_result(result)) {
    if (!result.ok && result.sent == 0) {
      ESP_LOGD(TAG, "Kern 1: Modul nimmt kein Byte an (%s)", ipc::send_error_name(result.error));
    } else if (!result.ok) {
      ESP_LOGW(TAG, "Kern 1: Befehl nach %u Bytes abgebrochen (%s)", static_cast<unsigned>(result.sent),
               ipc::send_error_name(result.error));
    }
    this->scheduler_.on_send_result(result.id, result.ok, result.sent, now);
  }

  Frame frame;
  uint8_t byte = 0;
  while (ipc::poll_spi_byte(byte)) {
    now = millis();
    if (this->decoder_.feed(byte, now, frame)) {
      this->handle_frame_(frame, now);
    }
  }
  now = millis();
  if (this->decoder_.poll_timeout(now, frame)) {
    this->handle_frame_(frame, now);
  }

  switch (this->start_state_) {
    case StartState::WaitNetwork:
      if (network::is_connected()) {
        this->start_state_ = StartState::Settle;
        this->settle_since_ms_ = now;
      }
      break;
    case StartState::Settle:
      if (now - this->settle_since_ms_ >= MODULE_START_DELAY_MS) {
        this->start_init();
      }
      break;
    case StartState::Running:
      this->scheduler_.service(now, this->decoder_.open());
      break;
  }
}

void WiSafe2Component::dump_config() {
  ESP_LOGCONFIG(TAG, "WiSafe2:");
  LOG_TEXT_SENSOR("  ", "Status", this->status_sensor_);
  ESP_LOGCONFIG(TAG, "  SPI-Slave Kern 1: GP16 MOSI, GP17 CS, GP18 SCK, GP19 MISO, GP20 IRQ");
  ESP_LOGCONFIG(TAG, "  Rahmen maskiert, 500 ms Ruhe zwischen Befehlen, Init D3 19 50 00");
}

}  // namespace wisafe2
}  // namespace esphome
