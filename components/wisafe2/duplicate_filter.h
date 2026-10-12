#pragma once

#include <cstddef>
#include <cstdint>

namespace esphome {
namespace wisafe2 {

// Erkennt Funknachrichten, die mehrfach ankommen (73 kam 2× in 1 s, C8 3× in 1 ms), über SID, Typ und
// Sequenznummer. Die Sequenznummer ist 4 Bit breit und wiederholt sich je Absender nach 16 Nachrichten.
class DuplicateFilter {
 public:
  static constexpr uint32_t WINDOW_MS = 60000;

  // true, wenn dieselbe Nachricht in den letzten 60 s schon kam. Merkt sie sich sonst.
  bool seen(uint8_t sid, uint8_t code, uint8_t seq, uint32_t now_ms);
  void clear();

 protected:
  static constexpr size_t ENTRIES = 32;

  struct Entry {
    bool used{false};
    uint8_t sid{0};
    uint8_t code{0};
    uint8_t seq{0};
    uint32_t at_ms{0};
  };

  Entry entries_[ENTRIES]{};
  size_t next_{0};
};

}  // namespace wisafe2
}  // namespace esphome
