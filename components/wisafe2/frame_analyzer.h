#pragma once

#include "framing.h"

#include <cstddef>

namespace esphome {
namespace wisafe2 {

// Schreibt eine Zeile zum Rahmen nach out.
void describe_frame(const Frame &frame, char *out, size_t out_len);

}  // namespace wisafe2
}  // namespace esphome
