#include "wisafe2.h"

#include "core_ipc.h"
#include "esphome/core/log.h"

#include <Arduino.h>

namespace esphome {
namespace wisafe2 {
namespace ipc {

namespace {

SpscQueue<BlinkCommand, 8> blink_commands;
SpscQueue<LogMessage, 8> core1_logs;
std::atomic<uint32_t> dropped_logs{0};

}  // namespace

bool send_blink_command(bool enabled) { return blink_commands.push(BlinkCommand{static_cast<uint8_t>(enabled ? 1 : 0)}); }

bool poll_blink_command(bool &enabled) {
  BlinkCommand command{};
  if (!blink_commands.pop(command)) {
    return false;
  }
  enabled = command.enabled != 0;
  return true;
}

bool core1_log(const char *message) {
  LogMessage slot{};
  copy_text(slot.text, LogMessage::TEXT_LEN, message);
  if (!core1_logs.push(slot)) {
    dropped_logs.fetch_add(1, std::memory_order_relaxed);
    return false;
  }
  return true;
}

bool poll_core1_log(char *out, size_t out_len) {
  if (out == nullptr || out_len == 0) {
    return false;
  }
  LogMessage message{};
  if (!core1_logs.pop(message)) {
    return false;
  }
  copy_text(out, out_len, message.text);
  return true;
}

uint32_t take_dropped_logs() { return dropped_logs.exchange(0, std::memory_order_relaxed); }

}  // namespace ipc

static const char *const TAG = "wisafe2";

void WiSafe2Component::send_blink_enabled(bool enabled) {
  if (!ipc::send_blink_command(enabled)) {
    ESP_LOGW(TAG, "Befehl an Kern 1 verworfen");
  }
}

void WiSafe2BlinkSwitch::setup() {
  const bool initial_state = this->get_initial_state_with_restore_mode().value_or(true);
  this->control(initial_state);
}

void WiSafe2BlinkSwitch::write_state(bool state) {
  if (this->parent_ != nullptr) {
    this->parent_->send_blink_enabled(state);
  }
  this->publish_state(state);
}

void WiSafe2Component::setup() {
  ESP_LOGI(TAG, "Komponente geladen, SPI ist noch nicht aktiv");
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
}

void WiSafe2Component::dump_config() {
  ESP_LOGCONFIG(TAG, "WiSafe2:");
  LOG_TEXT_SENSOR("  ", "Status", this->status_sensor_);
  LOG_SWITCH("  ", "Blinken", this->blink_switch_);
  ESP_LOGCONFIG(TAG, "  Kern 1: Blinken folgt dem Schalter");
  ESP_LOGCONFIG(TAG, "  Log von Kern 1: Warteschlange nach Kern 0");
}

}  // namespace wisafe2
}  // namespace esphome

// Starke Symbole für den Arduino-Kern. Sie müssen im globalen Namensraum
// liegen, sonst startet Kern 1 nicht. Der eigene 8-KB-Stack verhindert,
// dass sich Kern 0 und Kern 1 die 8 KB teilen.
bool core1_separate_stack = true;

void setup1() {}

void loop1() {
  static bool blink_enabled = false;
  static bool led_on = false;
  static uint32_t next_ms = 0;

  bool enabled = false;
  while (esphome::wisafe2::ipc::poll_blink_command(enabled)) {
    blink_enabled = enabled;
    esphome::wisafe2::ipc::core1_log(enabled ? "Kern 1: Schalter umgelegt, Blinken ein"
                                             : "Kern 1: Schalter umgelegt, Blinken aus");
    if (enabled) {
      led_on = true;
      next_ms = millis() + 1000;
      digitalWrite(PIN_LED, HIGH);
    } else if (led_on) {
      led_on = false;
      digitalWrite(PIN_LED, LOW);
    }
  }

  if (!blink_enabled) {
    delay(5);
    return;
  }

  const uint32_t now = millis();
  if (static_cast<int32_t>(now - next_ms) < 0) {
    delay(5);
    return;
  }

  next_ms = now + 1000;
  led_on = !led_on;
  digitalWrite(PIN_LED, led_on ? HIGH : LOW);
}
