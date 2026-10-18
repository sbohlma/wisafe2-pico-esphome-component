#include "core_ipc.h"

#include "hardware/address_mapped.h"
#include "hardware/gpio.h"
#include "hardware/regs/spi.h"
#include "hardware/resets.h"
#include "hardware/spi.h"
#include "hardware/timer.h"
#include "pico/platform.h"

#include <cstdio>
#include <cstring>

namespace esphome {
namespace wisafe2 {
namespace ipc {

SpscQueue<LogMessage, 8> core1_logs;
SpscQueue<SpiByte, 256> spi_bytes;
SpscQueue<Command, 8> commands;
SpscQueue<CommandResult, 8> command_results;
std::atomic<uint32_t> dropped_logs{0};
std::atomic<bool> spi_setup_requested{false};

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

bool poll_spi_byte(uint8_t &byte, uint32_t &us) {
  SpiByte item{};
  if (!spi_bytes.pop(item)) {
    return false;
  }
  byte = item.byte;
  us = item.us;
  return true;
}

bool send_command(uint8_t id, const uint8_t *bytes, size_t len) {
  if (bytes == nullptr || len == 0 || len > Command::MAX_LEN) {
    return false;
  }
  Command command{};
  command.id = id;
  command.len = static_cast<uint8_t>(len);
  std::memcpy(command.bytes, bytes, len);
  return commands.push(command);
}

bool poll_command_result(CommandResult &result) { return command_results.pop(result); }

void request_spi_setup() { spi_setup_requested.store(true, std::memory_order_release); }

}  // namespace ipc
}  // namespace wisafe2
}  // namespace esphome

namespace {

namespace ipc = esphome::wisafe2::ipc;

constexpr unsigned int PIN_MOSI_FROM_RADIO = 16;
constexpr unsigned int PIN_CS = 17;
constexpr unsigned int PIN_SCK = 18;
constexpr unsigned int PIN_MISO_TO_RADIO = 19;
constexpr unsigned int PIN_IRQ = 20;

constexpr uint32_t CS_WAIT_US = 5000;
constexpr uint32_t IRQ_GAP_US = 10;

bool last_cs = true;
bool have_byte = false;
uint8_t latest_byte = 0;
bool spi_drop_pending = false;
bool spi_overrun_pending = false;
uint32_t last_fault_log_us = 0;

bool spi_ready = false;
ipc::Command tx_command{};
bool tx_active = false;
uint8_t tx_index = 0;

void report_spi_faults() {
  if (spi_drop_pending) {
    ipc::core1_log("SPI: Bytes verworfen, Warteschlange voll");
    spi_drop_pending = false;
  }
  if (spi_overrun_pending) {
    ipc::core1_log("SPI: Hardware-Überlauf");
    spi_overrun_pending = false;
  }
  last_fault_log_us = timer_hw->timerawl;
}

void __not_in_flash_func(busy_wait_short)(uint32_t us) {
  const uint32_t start = timer_hw->timerawl;
  while ((uint32_t) (timer_hw->timerawl - start) < us) {
  }
}

void __not_in_flash_func(pulse_irq_ack)() {
  gpio_put(PIN_IRQ, true);
  busy_wait_short(8);
  gpio_put(PIN_IRQ, false);
}

// Der Slave schiebt pro Byte den ältesten FIFO-Eintrag auf MISO. Der TX-FIFO hält deshalb immer genau
// einen Eintrag. Nur ein Reset des Blocks leert ihn, daher dieser Weg vor jedem Byte zum Modul.
void __not_in_flash_func(spi_restart)(uint8_t first_tx) {
  reset_block(RESETS_RESET_SPI0_BITS);
  unreset_block_wait(RESETS_RESET_SPI0_BITS);
  spi_hw_t *hw = spi_get_hw(spi0);
  hw->cpsr = 2;
  hw->cr0 = (8u - 1u) << SPI_SSPCR0_DSS_LSB;
  hw->cr1 = SPI_SSPCR1_MS_BITS;
  hw->dr = first_tx;
  hw->cr1 = SPI_SSPCR1_MS_BITS | SPI_SSPCR1_SSE_BITS;
}

void __not_in_flash_func(preload_tx_zero)() {
  if (spi_is_writable(spi0)) {
    spi_get_hw(spi0)->dr = 0;
  }
}

void __not_in_flash_func(push_rx_byte)(uint8_t byte) {
  if (!ipc::spi_bytes.push(ipc::SpiByte{byte, timer_hw->timerawl})) {
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
    pulse_irq_ack();
    if (have_byte) {
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

// IRQ bleibt für ein Byte High. Das Modul taktet, MISO gibt das Byte aus.
ipc::SendError __not_in_flash_func(write_byte_to_radio)(uint8_t byte) {
  gpio_put(PIN_IRQ, false);
  if (!wait_cs(true, CS_WAIT_US)) {
    return ipc::SendError::CsBusy;
  }
  busy_wait_short(IRQ_GAP_US);
  spi_restart(byte);
  gpio_put(PIN_IRQ, true);
  const bool started = wait_cs(false, CS_WAIT_US);
  const bool finished = started && wait_cs(true, CS_WAIT_US);
  gpio_put(PIN_IRQ, false);
  spi_restart(0);
  have_byte = false;
  last_cs = gpio_get(PIN_CS);
  if (!started) {
    return ipc::SendError::NoClock;
  }
  return finished ? ipc::SendError::None : ipc::SendError::Incomplete;
}

void __not_in_flash_func(finish_command)(ipc::SendError error) {
  ipc::command_results.push(
      ipc::CommandResult{tx_command.id, tx_index, error == ipc::SendError::None, error, timer_hw->timerawl});
  tx_active = false;
}

void __not_in_flash_func(service_tx)() {
  if (!tx_active) {
    // Ein Byte vom Modul ist gerade unterwegs. Der Block-Reset beim Senden würde es verwerfen.
    if (!last_cs || have_byte) {
      return;
    }
    if (!ipc::commands.pop(tx_command)) {
      return;
    }
    tx_active = true;
    tx_index = 0;
  }
  const ipc::SendError error = write_byte_to_radio(tx_command.bytes[tx_index]);
  if (error != ipc::SendError::None) {
    finish_command(error);
    return;
  }
  tx_index++;
  if (tx_index >= tx_command.len) {
    finish_command(ipc::SendError::None);
  }
}

void __not_in_flash_func(poll_faults)() {
  const bool fault = spi_drop_pending || spi_overrun_pending;
  if (fault && (uint32_t) (timer_hw->timerawl - last_fault_log_us) >= 1000000u) {
    report_spi_faults();
  }
}

const char *level_name(unsigned int pin) { return gpio_get(pin) ? "High" : "Low"; }

void irq_low() {
  gpio_init(PIN_IRQ);
  gpio_set_dir(PIN_IRQ, GPIO_OUT);
  gpio_put(PIN_IRQ, false);
}

void spi_slave_setup() {
  irq_low();
  spi_restart(0);
  gpio_set_function(PIN_MOSI_FROM_RADIO, GPIO_FUNC_SPI);
  gpio_set_function(PIN_CS, GPIO_FUNC_SPI);
  gpio_set_function(PIN_SCK, GPIO_FUNC_SPI);
  gpio_set_function(PIN_MISO_TO_RADIO, GPIO_FUNC_SPI);
  last_cs = gpio_get(PIN_CS);
  have_byte = false;
  spi_ready = true;

  char text[ipc::LogMessage::TEXT_LEN];
  std::snprintf(text, sizeof(text), "SPI-Slave gestartet, CS %s, SCK %s, MOSI %s", level_name(PIN_CS),
                level_name(PIN_SCK), level_name(PIN_MOSI_FROM_RADIO));
  ipc::core1_log(text);
}

}  // namespace

bool core1_separate_stack = true;

// arduino-pico startet Kern 1 nur, wenn es setup1/loop1 unter ihrem C-Namen findet.
// Der SPI-Slave wird erst auf Anforderung von Kern 0 eingerichtet. Bis dahin hält Kern 1 nur IRQ auf Low.
extern "C" void setup1() { irq_low(); }

extern "C" void __not_in_flash_func(loop1)() {
  if (!tx_active && ipc::spi_setup_requested.exchange(false, std::memory_order_acquire)) {
    spi_slave_setup();
  }
  if (!spi_ready) {
    return;
  }
  if (!tx_active) {
    sniff_poll();
  }
  service_tx();
  poll_faults();
}
