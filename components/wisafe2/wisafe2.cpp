#include "wisafe2.h"

#include "esphome/core/log.h"

#include <Arduino.h>

namespace esphome {
namespace wisafe2 {

static const char *const TAG = "wisafe2";

void WiSafe2Component::setup() {
  ESP_LOGI(TAG, "Komponente geladen, SPI ist noch nicht aktiv");
  ESP_LOGI(TAG, "Kern 1 schaltet die Onboard-LED im Sekundentakt");
  if (this->status_sensor_ != nullptr) {
    this->status_sensor_->publish_state("bereit");
  }
}

void WiSafe2Component::dump_config() {
  ESP_LOGCONFIG(TAG, "WiSafe2:");
  LOG_TEXT_SENSOR("  ", "Status", this->status_sensor_);
  ESP_LOGCONFIG(TAG, "  Kern 1: Onboard-LED, 1 s an / 1 s aus");
}

}  // namespace wisafe2
}  // namespace esphome

// Starke Symbole für den Arduino-Kern. Sie müssen im globalen Namensraum
// liegen, sonst startet Kern 1 nicht. Der eigene 8-KB-Stack verhindert,
// dass sich Kern 0 und Kern 1 die 8 KB teilen.
bool core1_separate_stack = true;

void setup1() {}

void loop1() {
  static uint32_t next_ms = 0;
  static bool led_on = false;

  const uint32_t now = millis();
  if (static_cast<int32_t>(now - next_ms) < 0) {
    delay(5);
    return;
  }

  next_ms = now + 1000;
  led_on = !led_on;
  digitalWrite(PIN_LED, led_on ? HIGH : LOW);
}
