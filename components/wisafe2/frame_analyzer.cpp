#include "frame_analyzer.h"

#include <cstdio>
#include <cstring>

namespace esphome {
namespace wisafe2 {
namespace {

void append_text(char *out, size_t out_len, const char *text) {
  if (out_len == 0) {
    return;
  }
  const size_t used = std::strlen(out);
  if (used >= out_len) {
    out[out_len - 1] = '\0';
    return;
  }
  std::snprintf(out + used, out_len - used, "%s", text == nullptr ? "" : text);
}

void append_hex_byte(char *out, size_t out_len, uint8_t byte) {
  char tmp[4];
  std::snprintf(tmp, sizeof(tmp), "%02X", byte);
  append_text(out, out_len, tmp);
}

void append_hex_frame(char *out, size_t out_len, const uint8_t *frame, size_t length) {
  for (size_t index = 0; index < length; ++index) {
    if (index != 0) {
      append_text(out, out_len, " ");
    }
    append_hex_byte(out, out_len, frame[index]);
  }
}

void append_id(char *out, size_t out_len, uint8_t b0, uint8_t b1, uint8_t b2) {
  char tmp[8];
  std::snprintf(tmp, sizeof(tmp), "%02x%02x%02x", b0, b1, b2);
  append_text(out, out_len, tmp);
}

const char *model_name(uint16_t model) {
  switch (model) {
    case 0xED08:
      return "FP2620W2";
    case 0x1103:
      return "WST-630";
    case 0x1104:
      return "FP1720W2-R";
    case 0x7803:
      return "W2-CO-10X";
    case 0xC304:
      return "W2-SVP-630";
    default:
      return nullptr;
  }
}

const char *trigger_name(uint8_t trigger) {
  switch (trigger) {
    case 0x81:
      return "Rauch";
    case 0x82:
      return "Hitze";
    case 0x41:
      return "Kohlenmonoxid";
    case 0xFF:
      return "Alle";
    default:
      return nullptr;
  }
}

void append_model(char *out, size_t out_len, uint8_t hi, uint8_t lo) {
  const uint16_t model = (static_cast<uint16_t>(hi) << 8) | lo;
  char tmp[8];
  std::snprintf(tmp, sizeof(tmp), "%04x", model);
  append_text(out, out_len, tmp);
  if (const char *name = model_name(model)) {
    append_text(out, out_len, " ");
    append_text(out, out_len, name);
  }
}

void append_trigger(char *out, size_t out_len, uint8_t trigger) {
  if (const char *name = trigger_name(trigger)) {
    append_text(out, out_len, name);
    return;
  }
  append_text(out, out_len, "Auslöser ");
  append_hex_byte(out, out_len, trigger);
}

void append_raw_suffix(char *out, size_t out_len, const uint8_t *frame, size_t length) {
  append_text(out, out_len, " [");
  append_hex_frame(out, out_len, frame, length);
  append_text(out, out_len, "]");
}

}  // namespace

void describe_frame(const uint8_t *frame, size_t length, bool complete, char *out, size_t out_len) {
  if (out_len == 0) {
    return;
  }
  out[0] = '\0';
  if (frame == nullptr || length == 0 || !complete || frame[length - 1] != 0x7E) {
    append_text(out, out_len, "Roh unvollständig: ");
    if (frame != nullptr && length > 0) {
      append_hex_frame(out, out_len, frame, length);
    }
    return;
  }

  const uint8_t type = frame[0];
  if (type == 0x70 && length == 11) {
    append_text(out, out_len, "Test Gerät ");
    append_id(out, out_len, frame[1], frame[2], frame[3]);
    append_text(out, out_len, ", Modell ");
    append_model(out, out_len, frame[6], frame[7]);
    append_text(out, out_len, ", ");
    append_trigger(out, out_len, frame[4]);
    append_text(out, out_len, frame[5] == 0x01 ? ", bestanden" : ", nicht bestanden");
    char seq[16];
    std::snprintf(seq, sizeof(seq), ", Folge %02x", frame[9]);
    append_text(out, out_len, seq);
    append_raw_suffix(out, out_len, frame, length);
    return;
  }

  if (type == 0x71 && length == 10) {
    append_text(out, out_len, "Sockel Gerät ");
    append_id(out, out_len, frame[1], frame[2], frame[3]);
    append_text(out, out_len, ", Modell ");
    append_model(out, out_len, frame[4], frame[5]);
    append_text(out, out_len, (frame[6] & 0x04) != 0 ? ", Sockel an" : ", Sockel aus");
    append_text(out, out_len, (frame[6] & 0x42) != 0 ? ", Batterie niedrig" : ", Batterie ok");
    char seq[16];
    std::snprintf(seq, sizeof(seq), ", Folge %02x", frame[8]);
    append_text(out, out_len, seq);
    append_raw_suffix(out, out_len, frame, length);
    return;
  }

  if (type == 0x50 && length >= 6) {
    append_text(out, out_len, "Alarm Gerät ");
    append_id(out, out_len, frame[1], frame[2], frame[3]);
    append_text(out, out_len, ", ");
    append_trigger(out, out_len, frame[4]);
    append_raw_suffix(out, out_len, frame, length);
    return;
  }

  if (type == 0x61 && length >= 5) {
    append_text(out, out_len, "Stille Gerät ");
    append_id(out, out_len, frame[1], frame[2], frame[3]);
    append_raw_suffix(out, out_len, frame, length);
    return;
  }

  if (type == 0xD2 && length == 14) {
    append_text(out, out_len, "Gerät fehlt ");
    append_id(out, out_len, frame[6], frame[7], frame[8]);
    append_text(out, out_len, ", gemeldet von ");
    append_id(out, out_len, frame[1], frame[2], frame[3]);
    append_raw_suffix(out, out_len, frame, length);
    return;
  }

  if (type == 0x46 && length == 2) {
    append_text(out, out_len, "Antwort bereit 46 7E");
    return;
  }

  if (type == 0x41 && length == 2) {
    append_text(out, out_len, "Antwort bestätigt 41 7E");
    return;
  }

  if (type == 0xD4 && length >= 11) {
    append_text(out, out_len, frame[2] == 0x00 ? "Kopplung: nicht gekoppelt" : "Kopplung: gekoppelt");
    append_raw_suffix(out, out_len, frame, length);
    return;
  }

  append_text(out, out_len, "Roh: ");
  append_hex_frame(out, out_len, frame, length);
}

}  // namespace wisafe2
}  // namespace esphome
