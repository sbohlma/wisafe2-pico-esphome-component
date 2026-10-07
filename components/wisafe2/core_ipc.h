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

// SPI-Bytes: Kern 1 schreibt, Kern 0 liest.
bool poll_spi_byte(uint8_t &byte);

}  // namespace ipc
}  // namespace wisafe2
}  // namespace esphome
