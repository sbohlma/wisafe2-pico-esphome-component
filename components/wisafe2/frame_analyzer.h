#pragma once

#include "framing.h"

#include <cstddef>

namespace esphome {
namespace wisafe2 {

// Schreibt eine Zeile zum Rahmen nach out. raw_suffix hängt die Bytes des Rahmens in eckigen Klammern an.
void describe_frame(const Frame &frame, char *out, size_t out_len, bool raw_suffix = true);

// Zeile für den Schalter „Rohrahmen loggen“: Richtung, Zeit seit dem Start in s mit µs, Abstand zur vorigen Zeile
// und die Bytes wie auf der Leitung, also maskiert und mit 7E. truncated hängt „…“ an, note steht in Klammern am
// Ende.
void format_raw_line(const char *direction, uint64_t us, bool have_delta, int64_t delta_us, const uint8_t *bytes,
                     size_t len, bool truncated, const char *note, char *out, size_t out_len);

}  // namespace wisafe2
}  // namespace esphome
