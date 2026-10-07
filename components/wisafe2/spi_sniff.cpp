#include "core_ipc.h"

#include "hardware/gpio.h"
#include "hardware/regs/spi.h"
#include "hardware/spi.h"
#include "hardware/timer.h"
#include "pico/platform.h"

namespace esphome {
namespace wisafe2 {
namespace ipc {

SpscQueue<LogMessage, 8> core1_logs;
SpscQueue<uint8_t, 256> spi_bytes;
std::atomic<uint32_t> dropped_logs{0};

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

bool poll_spi_byte(uint8_t &byte) { return spi_bytes.pop(byte); }

}  // namespace ipc
}  // namespace wisafe2
}  // namespace esphome

namespace {

constexpr unsigned int PIN_MOSI_FROM_RADIO = 16;
constexpr unsigned int PIN_CS = 17;
constexpr unsigned int PIN_SCK = 18;
constexpr unsigned int PIN_MISO_TO_RADIO = 19;
constexpr unsigned int PIN_IRQ = 20;

bool last_cs = true;
bool have_byte = false;
uint8_t latest_byte = 0;
bool spi_drop_pending = false;
bool spi_overrun_pending = false;
uint32_t last_fault_log_us = 0;

void report_spi_faults() {
  if (spi_drop_pending) {
    esphome::wisafe2::ipc::core1_log("SPI: Bytes verworfen, Warteschlange voll");
    spi_drop_pending = false;
  }
  if (spi_overrun_pending) {
    esphome::wisafe2::ipc::core1_log("SPI: Hardware-Überlauf");
    spi_overrun_pending = false;
  }
  last_fault_log_us = timer_hw->timerawl;
}

void __not_in_flash_func(pulse_irq_ack)() {
  gpio_put(PIN_IRQ, true);
  const uint32_t start = timer_hw->timerawl;
  while ((uint32_t) (timer_hw->timerawl - start) < 8u) {
  }
  gpio_put(PIN_IRQ, false);
}

void __not_in_flash_func(preload_tx_zero)() {
  if (spi_is_writable(spi0)) {
    spi_get_hw(spi0)->dr = 0;
  }
}

void __not_in_flash_func(sniff_poll)() {
  if (spi_is_readable(spi0)) {
    latest_byte = static_cast<uint8_t>(spi_get_hw(spi0)->dr);
    have_byte = true;
    preload_tx_zero();
  }

  const bool cs = gpio_get(PIN_CS);
  if (!last_cs && cs) {
    if (!have_byte) {
      const uint32_t start = timer_hw->timerawl;
      while (!spi_is_readable(spi0) && (uint32_t) (timer_hw->timerawl - start) < 20u) {
      }
      if (spi_is_readable(spi0)) {
        latest_byte = static_cast<uint8_t>(spi_get_hw(spi0)->dr);
        have_byte = true;
        preload_tx_zero();
      }
    }
    if (have_byte) {
      pulse_irq_ack();
      if (!esphome::wisafe2::ipc::spi_bytes.push(latest_byte)) {
        spi_drop_pending = true;
      }
      have_byte = false;
    }
    while (spi_is_readable(spi0)) {
      latest_byte = static_cast<uint8_t>(spi_get_hw(spi0)->dr);
      preload_tx_zero();
      pulse_irq_ack();
      if (!esphome::wisafe2::ipc::spi_bytes.push(latest_byte)) {
        spi_drop_pending = true;
      }
    }
  }
  last_cs = cs;

  if ((spi_get_hw(spi0)->ris & SPI_SSPRIS_RORRIS_BITS) != 0) {
    spi_get_hw(spi0)->icr = SPI_SSPICR_RORIC_BITS;
    spi_overrun_pending = true;
  }
}

}  // namespace

bool core1_separate_stack = true;

void setup1() {
  gpio_init(PIN_IRQ);
  gpio_set_dir(PIN_IRQ, GPIO_OUT);
  gpio_put(PIN_IRQ, false);

  spi_init(spi0, 1000 * 1000);
  spi_set_format(spi0, 8, SPI_CPOL_0, SPI_CPHA_0, SPI_MSB_FIRST);
  spi_set_slave(spi0, true);
  gpio_set_function(PIN_MOSI_FROM_RADIO, GPIO_FUNC_SPI);
  gpio_set_function(PIN_CS, GPIO_FUNC_SPI);
  gpio_set_function(PIN_SCK, GPIO_FUNC_SPI);
  gpio_set_function(PIN_MISO_TO_RADIO, GPIO_FUNC_SPI);

  while (spi_is_readable(spi0)) {
    (void) spi_get_hw(spi0)->dr;
  }
  for (int index = 0; index < 8 && spi_is_writable(spi0); ++index) {
    spi_get_hw(spi0)->dr = 0;
  }
  spi_get_hw(spi0)->icr = SPI_SSPICR_RORIC_BITS;
  last_cs = gpio_get(PIN_CS);
  esphome::wisafe2::ipc::core1_log("SPI-Slave gestartet, nur Empfang");
}

void __not_in_flash_func(loop1)() {
  sniff_poll();
  const bool fault = spi_drop_pending || spi_overrun_pending;
  if (fault && (uint32_t) (timer_hw->timerawl - last_fault_log_us) >= 1000000u) {
    report_spi_faults();
  }
}
