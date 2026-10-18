#include "frame_analyzer.h"

#include "catalog.h"
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

void append_device(char *out, size_t out_len, const RadioMessage &msg) {
  append_text(out, out_len, "Gerät ");
  append_id(out, out_len, msg.device_id);
  if (is_gateway_id(msg.device_id)) {
    append_text(out, out_len, " (Gateway)");
  }
}

void append_model(char *out, size_t out_len, uint16_t model) {
  append_format(out, out_len, "Modell %u", model);
  const char *name = model_name(model);
  append_text(out, out_len, name != nullptr ? " " : " (unbekannt)");
  append_text(out, out_len, name);
}

void append_class(char *out, size_t out_len, uint8_t device_class) {
  if (const char *name = class_name(device_class)) {
    append_text(out, out_len, name);
    return;
  }
  append_format(out, out_len, "Klasse %02X", device_class);
}

void append_sender(char *out, size_t out_len, const RadioMessage &msg) {
  append_format(out, out_len, ", SID %u, Folge %u", msg.sid, msg.seq);
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

void append_fault_flags(char *out, size_t out_len, uint8_t flags) {
  append_format(out, out_len, "Flags %02X: ", flags);
  append_text(out, out_len, (flags & 0x04) != 0 ? "auf Sockel" : "abgenommen");
  for (unsigned int bit = 0x01; bit <= 0x80; bit <<= 1) {
    if (bit == 0x04 || (flags & bit) == 0) {
      continue;
    }
    append_text(out, out_len, ", ");
    append_text(out, out_len, fault_flag_name(static_cast<uint8_t>(bit)));
  }
}

void append_fault_details(char *out, size_t out_len, uint16_t model, uint16_t fs) {
  append_format(out, out_len, "FS %04X", fs);
  if (fs == 0) {
    append_text(out, out_len, " (keine Fehler)");
    return;
  }
  append_text(out, out_len, ":");
  bool first = true;
  for (int bit = 0; bit < 16; ++bit) {
    if ((fs & (1u << bit)) == 0) {
      continue;
    }
    append_text(out, out_len, first ? " " : ", ");
    first = false;
    if (const char *name = fault_detail_name(model, bit)) {
      append_text(out, out_len, name);
    } else {
      append_format(out, out_len, "Bit %u", static_cast<unsigned int>(bit));
    }
  }
}

bool describe_local(const Frame &frame, char *out, size_t out_len, bool raw_suffix) {
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
    case LocalType::InitReport:
      append_text(out, out_len, "Init-Antworten: ");
      append_sid_list(out, out_len, msg.map);
      append_format(out, out_len, ", eigene SID %u", msg.sid);
      break;
    case LocalType::RemoteSidMap:
      append_format(out, out_len, "Remote-SID-Map von SID %u: ", msg.sid);
      append_sid_list(out, out_len, msg.map);
      break;
    case LocalType::None:
      return false;
  }
  if (raw_suffix) {
    append_raw_suffix(out, out_len, frame);
  }
  return true;
}

void describe_request(char *out, size_t out_len, const char *what, const RadioMessage &msg) {
  append_text(out, out_len, what);
  append_text(out, out_len, " an ");
  append_device(out, out_len, msg);
  append_text(out, out_len, ", ");
  append_model(out, out_len, msg.model);
  append_format(out, out_len, ", von SID %u, Folge %u", msg.sid, msg.seq);
}

bool describe_radio(const Frame &frame, char *out, size_t out_len, bool raw_suffix) {
  RadioMessage msg;
  if (!parse_radio(frame, msg)) {
    return false;
  }
  switch (msg.type) {
    case RadioType::Alarm:
      append_text(out, out_len, "Alarm ");
      append_device(out, out_len, msg);
      append_text(out, out_len, ", ");
      append_class(out, out_len, msg.device_class);
      append_format(out, out_len, ", d5 %02X", msg.value);
      append_sender(out, out_len, msg);
      break;
    case RadioType::AlarmStop:
      append_text(out, out_len, "Alarm Ende ");
      append_device(out, out_len, msg);
      append_text(out, out_len, ", ");
      append_class(out, out_len, msg.device_class);
      append_sender(out, out_len, msg);
      break;
    case RadioType::Silence: {
      append_text(out, out_len, "Stummschaltung ");
      append_device(out, out_len, msg);
      const char *name = silence_class_name(msg.device_class);
      if (name != nullptr) {
        append_text(out, out_len, ", ");
        append_text(out, out_len, name);
      } else {
        append_format(out, out_len, ", Klasse %02X", msg.device_class);
      }
      append_format(out, out_len, ", d5 %02X", msg.value);
      append_sender(out, out_len, msg);
      break;
    }
    case RadioType::Test:
      append_text(out, out_len, "Test ");
      append_device(out, out_len, msg);
      append_text(out, out_len, ", ");
      append_class(out, out_len, msg.device_class);
      append_text(out, out_len, msg.value == 0x01 ? ", bestanden, " : ", nicht bestanden, ");
      append_model(out, out_len, msg.model);
      append_sender(out, out_len, msg);
      break;
    case RadioType::Fault:
      append_text(out, out_len, "Fehlerbericht ");
      append_device(out, out_len, msg);
      append_text(out, out_len, ", ");
      append_model(out, out_len, msg.model);
      append_text(out, out_len, ", ");
      append_fault_flags(out, out_len, msg.value);
      append_sender(out, out_len, msg);
      break;
    case RadioType::FaultDetailsRequest:
      describe_request(out, out_len, "Anfrage Fehlerdetails", msg);
      break;
    case RadioType::FaultDetails:
      append_text(out, out_len, "Fehlerdetails ");
      append_device(out, out_len, msg);
      append_text(out, out_len, ", ");
      append_model(out, out_len, msg.model);
      append_text(out, out_len, ", ");
      append_fault_details(out, out_len, msg.model, msg.value16);
      append_sender(out, out_len, msg);
      break;
    case RadioType::DiagnoseRequest:
      append_format(out, out_len, "Diagnoseanfrage von SID %u, Folge %u", msg.sid, msg.seq);
      break;
    case RadioType::DiagnoseResult:
      append_text(out, out_len, "Diagnoseergebnis ");
      append_device(out, out_len, msg);
      append_text(out, out_len, ", ");
      append_class(out, out_len, msg.device_class);
      append_text(out, out_len, ", ");
      append_model(out, out_len, msg.model);
      if (msg.value == 0x01) {
        append_text(out, out_len, ", bestanden");
      } else {
        append_format(out, out_len, ", Ergebnis %02X", msg.value);
      }
      append_sender(out, out_len, msg);
      break;
    case RadioType::IdRequest:
      append_format(out, out_len, "ID-Anfrage von SID %u, Folge %u", msg.sid, msg.seq);
      break;
    case RadioType::IdInfo:
      append_text(out, out_len, "ID-Information ");
      append_device(out, out_len, msg);
      append_text(out, out_len, ", ");
      append_class(out, out_len, msg.device_class);
      append_text(out, out_len, ", ");
      append_model(out, out_len, msg.model);
      append_sender(out, out_len, msg);
      break;
    case RadioType::FaultEventsRequest:
      describe_request(out, out_len, "Anfrage Fehlerereignisse", msg);
      break;
    case RadioType::FaultEvents:
      append_text(out, out_len, "Fehlerereignisse ");
      append_device(out, out_len, msg);
      append_text(out, out_len, ", ");
      append_hex_frame(out, out_len, msg.events, sizeof(msg.events));
      if (msg.events[0] == 0 && msg.events[1] == 0 && msg.events[2] == 0) {
        append_text(out, out_len, " (keine)");
      }
      append_sender(out, out_len, msg);
      break;
    case RadioType::BatteryRequest:
      describe_request(out, out_len, "Anfrage Batterie", msg);
      break;
    case RadioType::Battery:
      append_text(out, out_len, "Batterie ");
      append_device(out, out_len, msg);
      append_format(out, out_len, ", %u mV", msg.value16);
      append_sender(out, out_len, msg);
      break;
    case RadioType::UnknownD5:
      append_text(out, out_len, "D5 ");
      append_device(out, out_len, msg);
      append_text(out, out_len, ", ");
      append_model(out, out_len, msg.model);
      append_format(out, out_len, ", d6 %02X", msg.value);
      append_sender(out, out_len, msg);
      break;
    case RadioType::None:
      return false;
  }
  if (raw_suffix) {
    append_raw_suffix(out, out_len, frame);
  }
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

void describe_frame(const Frame &frame, char *out, size_t out_len, bool raw_suffix) {
  if (out_len == 0) {
    return;
  }
  out[0] = '\0';
  if (frame.status != FrameStatus::Complete || frame.len == 0) {
    append_text(out, out_len, status_prefix(frame.status));
    append_hex_frame(out, out_len, frame.data, frame.len);
    return;
  }
  if (describe_local(frame, out, out_len, raw_suffix) || describe_radio(frame, out, out_len, raw_suffix)) {
    return;
  }
  append_text(out, out_len, "Roh: ");
  append_hex_frame(out, out_len, frame.data, frame.len);
}

void format_raw_line(const char *direction, uint64_t us, bool have_delta, int64_t delta_us, const uint8_t *bytes,
                     size_t len, bool truncated, const char *note, char *out, size_t out_len) {
  if (out_len == 0) {
    return;
  }
  // Ohne 64-Bit-Formate: printf der Pico-Toolchain kennt %llu nicht sicher.
  int used = std::snprintf(out, out_len, "%s %lu.%06lu s", direction, static_cast<unsigned long>(us / 1000000u),
                           static_cast<unsigned long>(us % 1000000u));
  if (have_delta && used > 0 && static_cast<size_t>(used) < out_len) {
    const char sign = delta_us < 0 ? '-' : '+';
    const uint64_t magnitude = delta_us < 0 ? static_cast<uint64_t>(-delta_us) : static_cast<uint64_t>(delta_us);
    used += std::snprintf(out + used, out_len - used, " (%c%lu.%06lu s)", sign,
                          static_cast<unsigned long>(magnitude / 1000000u),
                          static_cast<unsigned long>(magnitude % 1000000u));
  }
  for (size_t index = 0; index < len && used > 0 && static_cast<size_t>(used) + 4 <= out_len; ++index) {
    used += std::snprintf(out + used, out_len - used, " %02X", bytes[index]);
  }
  if (truncated) {
    append_text(out, out_len, " …");
  }
  if (note != nullptr && note[0] != '\0') {
    append_text(out, out_len, " (");
    append_text(out, out_len, note);
    append_text(out, out_len, ")");
  }
}

}  // namespace wisafe2
}  // namespace esphome
