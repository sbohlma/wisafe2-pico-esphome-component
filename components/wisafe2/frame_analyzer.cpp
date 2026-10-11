#include "frame_analyzer.h"

#include "messages.h"

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

void append_format(char *out, size_t out_len, const char *format, unsigned int a, unsigned int b = 0) {
  char tmp[48];
  std::snprintf(tmp, sizeof(tmp), format, a, b);
  append_text(out, out_len, tmp);
}

void append_hex_byte(char *out, size_t out_len, uint8_t byte) { append_format(out, out_len, "%02X", byte); }

void append_hex_frame(char *out, size_t out_len, const uint8_t *frame, size_t length) {
  for (size_t index = 0; index < length; ++index) {
    if (index != 0) {
      append_text(out, out_len, " ");
    }
    append_hex_byte(out, out_len, frame[index]);
  }
}

void append_id(char *out, size_t out_len, const uint8_t *id) {
  char tmp[8];
  format_device_id(id, tmp, sizeof(tmp));
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
  append_format(out, out_len, "%04x", model);
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

void append_raw_suffix(char *out, size_t out_len, const Frame &frame) {
  append_text(out, out_len, " [");
  append_hex_frame(out, out_len, frame.data, frame.len);
  append_text(out, out_len, "]");
}

void append_sid_list(char *out, size_t out_len, uint64_t map) {
  append_format(out, out_len, "%u Knoten", static_cast<unsigned int>(count_sids(map)));
  if (map == 0) {
    return;
  }
  append_text(out, out_len, " (");
  bool first = true;
  for (unsigned int sid = 0; sid < 64; ++sid) {
    if (((map >> sid) & 1u) == 0) {
      continue;
    }
    append_format(out, out_len, first ? "%u" : ", %u", sid);
    first = false;
  }
  append_text(out, out_len, ")");
}

void append_rm_state(char *out, size_t out_len, const RmState &rm) {
  append_format(out, out_len, "Batterie %u mV, RSSI %u", rm_battery_mv(rm.bvnl), rm.rssi);
  append_format(out, out_len, ", FW %02X, ID ", rm.firmware);
  append_id(out, out_len, rm.device_id);
  append_format(out, out_len, ", kritisch %02X, Funkfehler %u", rm.critical, rm.radio_faults);
}

bool describe_local(const Frame &frame, char *out, size_t out_len) {
  LocalMessage msg;
  if (!parse_local(frame, msg)) {
    return false;
  }
  switch (msg.type) {
    case LocalType::Ack:
      append_text(out, out_len, "Quittung 46");
      return true;
    case LocalType::Reject:
      append_text(out, out_len, "Abgelehnt 47, SID nicht in der lokalen SID-Map");
      return true;
    case LocalType::IdentityRequest:
      append_text(out, out_len, "Identitätsanfrage 41");
      return true;
    case LocalType::RmStatus:
      append_text(out, out_len, "RM-Status: ");
      if (msg.sid == SID_UNPAIRED) {
        append_text(out, out_len, "ungekoppelt, ");
      } else {
        append_format(out, out_len, "SID %u, ", msg.sid);
      }
      append_rm_state(out, out_len, msg.rm);
      break;
    case LocalType::MissingMap:
      append_text(out, out_len, msg.reason == 0x04 ? "Nicht erreichbar nach D3 04: " : "Nicht erreichbar: ");
      append_sid_list(out, out_len, msg.map);
      break;
    case LocalType::LocalSidMap:
      append_text(out, out_len, "Lokale SID-Map: ");
      append_sid_list(out, out_len, msg.map);
      break;
    case LocalType::NewNetworkMap:
      append_text(out, out_len, "Neue Netzkarte: ");
      append_sid_list(out, out_len, msg.map);
      break;
    case LocalType::RemoteStatus:
      append_format(out, out_len, "Remote-Status SID %u: ", msg.sid);
      append_rm_state(out, out_len, msg.rm);
      break;
    case LocalType::PresenceHeard:
      append_format(out, out_len, "Präsenz SID %u, Gerät ", msg.sid);
      append_id(out, out_len, msg.device_id);
      break;
    case LocalType::RemoteSidMap:
      append_format(out, out_len, "Remote-SID-Map von SID %u: ", msg.sid);
      append_sid_list(out, out_len, msg.map);
      break;
    case LocalType::None:
      return false;
  }
  append_raw_suffix(out, out_len, frame);
  return true;
}

const char *status_prefix(FrameStatus status) {
  switch (status) {
    case FrameStatus::Timeout:
      return "Roh unvollständig: ";
    case FrameStatus::Overflow:
      return "Roh zu lang: ";
    case FrameStatus::BadEscape:
      return "Roh ungültige Maskierung: ";
    case FrameStatus::Complete:
      break;
  }
  return "Roh: ";
}

}  // namespace

void describe_frame(const Frame &frame, char *out, size_t out_len) {
  if (out_len == 0) {
    return;
  }
  out[0] = '\0';
  if (frame.status != FrameStatus::Complete || frame.len == 0) {
    append_text(out, out_len, status_prefix(frame.status));
    append_hex_frame(out, out_len, frame.data, frame.len);
    return;
  }
  if (describe_local(frame, out, out_len)) {
    return;
  }

  const uint8_t *d = frame.data;
  const uint8_t type = d[0];
  if (type == 0x70 && frame.len == 10) {
    append_text(out, out_len, "Test Gerät ");
    append_id(out, out_len, d + 1);
    append_text(out, out_len, ", Modell ");
    append_model(out, out_len, d[6], d[7]);
    append_text(out, out_len, ", ");
    append_trigger(out, out_len, d[4]);
    append_text(out, out_len, d[5] == 0x01 ? ", bestanden" : ", nicht bestanden");
    append_format(out, out_len, ", SID %u, Folge %u", d[8], d[9]);
    append_raw_suffix(out, out_len, frame);
    return;
  }

  if (type == 0x71 && frame.len == 9) {
    append_text(out, out_len, "Sockel Gerät ");
    append_id(out, out_len, d + 1);
    append_text(out, out_len, ", Modell ");
    append_model(out, out_len, d[4], d[5]);
    append_text(out, out_len, (d[6] & 0x04) != 0 ? ", Sockel an" : ", Sockel aus");
    append_text(out, out_len, (d[6] & 0x42) != 0 ? ", Batterie niedrig" : ", Batterie ok");
    append_format(out, out_len, ", SID %u, Folge %u", d[7], d[8]);
    append_raw_suffix(out, out_len, frame);
    return;
  }

  if (type == 0x50 && frame.len >= 5) {
    append_text(out, out_len, "Alarm Gerät ");
    append_id(out, out_len, d + 1);
    append_text(out, out_len, ", ");
    append_trigger(out, out_len, d[4]);
    append_raw_suffix(out, out_len, frame);
    return;
  }

  if (type == 0x61 && frame.len >= 4) {
    append_text(out, out_len, "Stille Gerät ");
    append_id(out, out_len, d + 1);
    append_raw_suffix(out, out_len, frame);
    return;
  }

  append_text(out, out_len, "Roh: ");
  append_hex_frame(out, out_len, frame.data, frame.len);
}

}  // namespace wisafe2
}  // namespace esphome
