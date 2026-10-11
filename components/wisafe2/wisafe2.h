#pragma once

#include "esphome/components/text_sensor/text_sensor.h"
#include "esphome/core/component.h"

#include <cstddef>
#include <cstdint>

namespace esphome {
namespace wisafe2 {

class WiSafe2Component : public Component {
 public:
  void set_status_sensor(text_sensor::TextSensor *sensor) { this->status_sensor_ = sensor; }

  void setup() override;
  void loop() override;
  void dump_config() override;
  float get_setup_priority() const override { return setup_priority::LATE; }

  // Reiht einen Befehl für Kern 1 ein. false, wenn die Warteschlange voll oder der Befehl zu lang ist.
  bool send_command(const uint8_t *bytes, size_t len, uint8_t *id = nullptr);
  // Startet den Init-Ablauf sofort, ohne die Wartezeit nach dem Start.
  void start_init();

 protected:
  enum class InitState : uint8_t { Settle, Send, AwaitSent, AwaitReply, Pause, Done };

  void emit_frame_(bool complete);
  void handle_command_result_(uint8_t id, bool ok);
  void service_init_();

  text_sensor::TextSensor *status_sensor_{nullptr};
  uint8_t frame_[32]{};
  size_t frame_len_{0};
  uint32_t last_byte_ms_{0};
  bool frame_open_{false};

  uint8_t next_command_id_{0};
  InitState init_state_{InitState::Settle};
  uint32_t init_mark_ms_{0};
  int init_attempt_{0};
  uint8_t init_command_id_{0};
};

}  // namespace wisafe2
}  // namespace esphome
