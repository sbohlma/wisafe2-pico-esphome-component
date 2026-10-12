#pragma once

#include "framing.h"

#include <cstddef>

namespace esphome {
namespace wisafe2 {

// Schreibt eine Zeile zum Rahmen nach out. raw_suffix hängt die Bytes des Rahmens in eckigen Klammern an.
void describe_frame(const Frame &frame, char *out, size_t out_len, bool raw_suffix = true);

}  // namespace wisafe2
}  // namespace esphome
