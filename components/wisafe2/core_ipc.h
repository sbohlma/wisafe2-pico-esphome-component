#pragma once

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <type_traits>

namespace esphome {
namespace wisafe2 {
namespace ipc {

// Eine Richtung, ein Schreiber, ein Leser. Keine Sperre: der Flash-Stillstand
// des jeweils anderen Kerns kann hier nicht hängen bleiben.
template<typename T, size_t N> class SpscQueue {
 public:
  static_assert(N >= 2 && (N & (N - 1)) == 0, "Kapazität muss Zweierpotenz sein");
  static_assert(std::is_trivially_copyable<T>::value, "Nur trivial kopierbare Nachrichten");

  __attribute__((always_inline)) inline bool push(const T &item) {
    const uint32_t head = this->head_.load(std::memory_order_relaxed);
    const uint32_t tail = this->tail_.load(std::memory_order_acquire);
    if (head - tail >= N) {
      return false;
    }
    this->slots_[head & (N - 1)] = item;
    this->head_.store(head + 1, std::memory_order_release);
    return true;
  }

  __attribute__((always_inline)) inline bool pop(T &item) {
    const uint32_t tail = this->tail_.load(std::memory_order_relaxed);
    const uint32_t head = this->head_.load(std::memory_order_acquire);
    if (tail == head) {
      return false;
    }
    item = this->slots_[tail & (N - 1)];
    this->tail_.store(tail + 1, std::memory_order_release);
    return true;
  }

 private:
  T slots_[N]{};
  std::atomic<uint32_t> head_{0};
  std::atomic<uint32_t> tail_{0};
};

struct LogMessage {
  static constexpr size_t TEXT_LEN = 96;
  char text[TEXT_LEN];
};

inline void copy_text(char *dest, size_t dest_len, const char *src) {
  if (dest_len == 0) {
    return;
  }
  size_t index = 0;
  if (src != nullptr) {
    while (index + 1 < dest_len && src[index] != '\0') {
      dest[index] = src[index];
      ++index;
    }
  }
  dest[index] = '\0';
}

// Kern 1 schreibt, Kern 0 liest und gibt die Zeile im ESPHome-Logger aus.
bool core1_log(const char *message);
bool poll_core1_log(char *out, size_t out_len);
uint32_t take_dropped_logs();

// Ein Befehl an das Modul. Kern 1 sendet seine Bytes direkt hintereinander.
struct Command {
  static constexpr size_t MAX_LEN = 32;
  uint8_t id;
  uint8_t len;
  uint8_t bytes[MAX_LEN];
};

enum class SendError : uint8_t {
  None,
  CsBusy,      // CS blieb Low, das Modul sendet gerade selbst
  NoClock,     // das Modul hat nach IRQ High nicht zu takten begonnen
  Incomplete,  // das Modul hat das Byte angefangen, aber nicht beendet
};

inline const char *send_error_name(SendError error) {
  switch (error) {
    case SendError::None:
      return "ok";
    case SendError::CsBusy:
      return "CS belegt";
    case SendError::NoClock:
      return "Modul taktet nicht";
    case SendError::Incomplete:
      return "Byte unvollständig";
  }
  return "?";
}

struct CommandResult {
  uint8_t id;
  uint8_t sent;
  bool ok;
  SendError error;
};

// SPI-Bytes: Kern 1 schreibt, Kern 0 liest.
bool poll_spi_byte(uint8_t &byte);

// Befehle: Kern 0 schreibt, Kern 1 sendet und meldet je Befehl ein Ergebnis zurück.
bool send_command(uint8_t id, const uint8_t *bytes, size_t len);
bool poll_command_result(CommandResult &result);

// Kern 0 fordert an, Kern 1 richtet SPI-Slave und IRQ ein, sobald kein Befehl unterwegs ist, und meldet
// die Pegel von CS, SCK und MOSI. Vor der ersten Anforderung sendet und empfängt Kern 1 nichts.
void request_spi_setup();

}  // namespace ipc
}  // namespace wisafe2
}  // namespace esphome
