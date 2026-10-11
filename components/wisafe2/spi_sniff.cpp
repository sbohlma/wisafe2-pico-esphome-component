#include "core_ipc.h"

#include "hardware/address_mapped.h"
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

constexpr uint32_t INIT_SETTLE_US = 5000000;
constexpr uint32_t INIT_REPLY_US = 125000;
constexpr uint32_t INIT_PAUSE_US = 500000;
constexpr uint32_t CS_WAIT_US = 5000;
constexpr int INIT_ATTEMPTS = 50;
constexpr int INIT_LEN = 5;

enum class InitPhase { Settle, Sending, WaitReply, Pause, Done };

bool last_cs = true;
bool have_byte = false;
uint8_t latest_byte = 0;
bool spi_drop_pending = false;
bool spi_overrun_pending = false;
uint32_t last_fault_log_us = 0;

InitPhase init_phase = InitPhase::Settle;
uint32_t init_mark_us = 0;
int init_attempt = 0;
int init_tx_index = 0;
bool init_saw_46 = false;
uint8_t init_cmd[INIT_LEN] = {0xD3, 0x19, 0x50, 0x00, 0x7E};

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

void note_init_reply(uint8_t byte) {
  if (init_phase == InitPhase::Done || init_phase == InitPhase::Settle || init_phase == InitPhase::Sending) {
    return;
  }
  if (init_saw_46 && byte == 0x7E) {
    init_phase = InitPhase::Done;
    init_saw_46 = false;
    return;
  }
  init_saw_46 = byte == 0x46;
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

void __not_in_flash_func(push_rx_byte)(uint8_t byte) {
  note_init_reply(byte);
  if (!esphome::wisafe2::ipc::spi_bytes.push(byte)) {
    spi_drop_pending = true;
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
      push_rx_byte(latest_byte);
      have_byte = false;
    }
    while (spi_is_readable(spi0)) {
      latest_byte = static_cast<uint8_t>(spi_get_hw(spi0)->dr);
      preload_tx_zero();
      pulse_irq_ack();
      push_rx_byte(latest_byte);
    }
  }
  last_cs = cs;

  if ((spi_get_hw(spi0)->ris & SPI_SSPRIS_RORRIS_BITS) != 0) {
    spi_get_hw(spi0)->icr = SPI_SSPICR_RORIC_BITS;
    spi_overrun_pending = true;
  }
}

bool __not_in_flash_func(wait_cs)(bool level, uint32_t timeout_us) {
  const uint32_t start = timer_hw->timerawl;
  while (gpio_get(PIN_CS) != level) {
    if ((uint32_t) (timer_hw->timerawl - start) >= timeout_us) {
      return false;
    }
  }
  return true;
}

void __not_in_flash_func(flush_spi_fifos)() {
  hw_clear_bits(&spi_get_hw(spi0)->cr1, SPI_SSPCR1_SSE_BITS);
  hw_set_bits(&spi_get_hw(spi0)->cr1, SPI_SSPCR1_MS_BITS);
  hw_set_bits(&spi_get_hw(spi0)->cr1, SPI_SSPCR1_SSE_BITS);
}

void __not_in_flash_func(discard_rx)() {
  while (spi_is_readable(spi0)) {
    (void) spi_get_hw(spi0)->dr;
  }
  if ((spi_get_hw(spi0)->ris & SPI_SSPRIS_RORRIS_BITS) != 0) {
    spi_get_hw(spi0)->icr = SPI_SSPICR_RORIC_BITS;
  }
  have_byte = false;
  last_cs = gpio_get(PIN_CS);
}

// IRQ bleibt für ein Byte High. Das Modul taktet, MISO gibt das Byte aus.
bool __not_in_flash_func(write_byte_to_radio)(uint8_t byte) {
  gpio_put(PIN_IRQ, false);
  if (!wait_cs(true, CS_WAIT_US)) {
    return false;
  }
  flush_spi_fifos();
  spi_get_hw(spi0)->dr = byte;
  gpio_put(PIN_IRQ, true);
  const bool started = wait_cs(false, CS_WAIT_US);
  const bool finished = started && wait_cs(true, CS_WAIT_US);
  gpio_put(PIN_IRQ, false);
  discard_rx();
  preload_tx_zero();
  return finished;
}

void __not_in_flash_func(service_init)() {
  const uint32_t now = timer_hw->timerawl;
  if (init_phase == InitPhase::Done) {
    return;
  }
  if (init_phase == InitPhase::Settle) {
    if ((uint32_t) (now - init_mark_us) >= INIT_SETTLE_US) {
      init_phase = InitPhase::Sending;
      init_tx_index = 0;
      init_saw_46 = false;
    }
    return;
  }
  if (init_phase == InitPhase::Sending) {
    (void) write_byte_to_radio(init_cmd[init_tx_index]);
    init_tx_index++;
    if (init_tx_index >= INIT_LEN) {
      init_phase = InitPhase::WaitReply;
      init_mark_us = timer_hw->timerawl;
      init_saw_46 = false;
    }
    return;
  }
  if (init_phase == InitPhase::WaitReply) {
    if ((uint32_t) (now - init_mark_us) >= INIT_REPLY_US) {
      init_phase = InitPhase::Pause;
      init_mark_us = now;
    }
    return;
  }
  if (init_phase == InitPhase::Pause) {
    if ((uint32_t) (now - init_mark_us) < INIT_PAUSE_US) {
      return;
    }
    if (init_attempt >= INIT_ATTEMPTS - 1) {
      init_phase = InitPhase::Done;
      return;
    }
    init_attempt++;
    init_tx_index = 0;
    init_saw_46 = false;
    init_phase = InitPhase::Sending;
  }
}

void __not_in_flash_func(poll_faults)() {
  const bool fault = spi_drop_pending || spi_overrun_pending;
  if (fault && (uint32_t) (timer_hw->timerawl - last_fault_log_us) >= 1000000u) {
    report_spi_faults();
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
  init_phase = InitPhase::Settle;
  init_mark_us = timer_hw->timerawl;
  init_attempt = 0;
  esphome::wisafe2::ipc::core1_log("SPI-Slave gestartet");
}

void __not_in_flash_func(loop1)() {
  if (init_phase != InitPhase::Sending) {
    sniff_poll();
  }
  service_init();
  poll_faults();
}
