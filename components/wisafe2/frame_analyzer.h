#pragma once

#include <cstddef>
#include <cstdint>

namespace esphome {
namespace wisafe2 {

// Schreibt eine Zeile nach out. complete ist falsch, wenn der Rahmen nicht mit 0x7E endete.
void describe_frame(const uint8_t *frame, size_t length, bool complete, char *out, size_t out_len);

}  // namespace wisafe2
}  // namespace esphome
