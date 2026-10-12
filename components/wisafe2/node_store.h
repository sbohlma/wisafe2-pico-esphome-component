#pragma once

#include <cstddef>
#include <cstdint>

namespace esphome {
namespace wisafe2 {

enum class StoreRead : uint8_t { Ok, Missing, Error };

// nodes.bin im LittleFS. ESPHome reserviert auf dem RP2 dafür 1 MB Flash (board_build.filesystem_size).
// Schreiben und Löschen halten Kern 1 an, solange der Flash beschäftigt ist (rp2040.idleOtherCore).
bool node_store_begin();
StoreRead node_store_read(uint8_t *data, size_t len, size_t &read);
bool node_store_write(const uint8_t *data, size_t len);
bool node_store_remove();

}  // namespace wisafe2
}  // namespace esphome
