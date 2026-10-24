#include "messages.h"

#include "catalog.h"

#include <cstdio>
#include <cstring>

namespace esphome {
namespace wisafe2 {
namespace {

struct RadioLayout {
  uint8_t code;
  RadioType type;
  // Länge mit Typ, SID und Sequenznummer.
  uint8_t len;
};

const RadioLayout RADIO_LAYOUTS[] = {
    {0x50, RadioType::Alarm, 8},
    {0x51, RadioType::AlarmStop, 7},
    {0x61, RadioType::Silence, 8},
    {0x70, RadioType::Test, 10},
    {0x71, RadioType::Fault, 9},
    {0x72, RadioType::FaultDetailsRequest, 9},
    {0x73, RadioType::FaultDetails, 10},
    {0xC1, RadioType::DiagnoseRequest, 10},
    {0xC2, RadioType::DiagnoseResult, 10},
    {0xC3, RadioType::IdRequest, 6},
    {0xC4, RadioType::IdInfo, 9},
    {0xC5, RadioType::FaultEventsRequest, 9},
    {0xC6, RadioType::FaultEvents, 11},
    {0xC7, RadioType::BatteryRequest, 9},
    {0xC8, RadioType::Battery, 11},
    {0xD5, RadioType::UnknownD5, 9},
};

void set_device(RadioMessage &out, const uint8_t *id) {
  out.has_device = true;
  std::memcpy(out.device_id, id, 3);
}

void set_model(RadioMessage &out, const uint8_t *bytes) {
  out.has_model = true;
  out.model = read_model(bytes);
}

void set_class(RadioMessage &out, uint8_t device_class) {
  out.has_class = true;
  out.device_class = device_class;
}

int hex_value(char c) {
  if (c >= '0' && c <= '9') {
    return c - '0';
  }
  if (c >= 'A' && c <= 'F') {
    return c - 'A' + 10;
  }
  if (c >= 'a' && c <= 'f') {
    return c - 'a' + 10;
  }
  return -1;
}

void read_rm_state(const uint8_t *d, RmState &out) {
  out.bvnl = d[0];
  out.bvwl = d[1];
  out.unknown = d[2];
  out.rssi = d[3];
  out.firmware = d[4];
  std::memcpy(out.device_id, d + 5, 3);
  out.critical = d[8];
  out.radio_faults = d[9];
}

bool parse_rm_report(const Frame &frame, LocalMessage &out) {
  const uint8_t *d = frame.data;
  if (frame.len < 2) {
    return false;
  }
  switch (d[1]) {
    case 0x01:
      if (frame.len < 11) {
        return false;
      }
      out.type = LocalType::MissingMap;
      out.reason = d[2];
      out.map = read_sid_map(d + 3);
      return true;
    case 0x03:
    case 0x04:
      if (frame.len < 10) {
        return false;
      }
      out.type = d[1] == 0x03 ? LocalType::LocalSidMap : LocalType::NewNetworkMap;
      out.map = read_sid_map(d + 2);
      return true;
    case 0x06:
      if (frame.len < 13) {
        return false;
      }
      out.type = LocalType::RemoteStatus;
      out.sid = d[2];
      read_rm_state(d + 3, out.rm);
      return true;
    case 0x09:
      if (frame.len < 6) {
        return false;
      }
      out.type = LocalType::PresenceHeard;
      out.sid = d[2];
      std::memcpy(out.device_id, d + 3, 3);
      return true;
    case 0x10:
      if (frame.len < 11) {
        return false;
      }
      out.type = LocalType::InitReport;
      out.map = read_sid_map(d + 2);
      out.sid = d[10];
      return true;
    case 0x11:
      if (frame.len < 12) {
        return false;
      }
      out.type = LocalType::RemoteSidMap;
      out.map = read_sid_map(d + 3);
      out.sid = d[11];
      return true;
    default:
      return false;
  }
}

}  // namespace

bool parse_local(const Frame &frame, LocalMessage &out) {
  out = LocalMessage{};
  if (frame.status != FrameStatus::Complete || frame.len == 0) {
    return false;
  }
  const uint8_t *d = frame.data;
  if (frame.len == 1) {
    switch (d[0]) {
      case MSG_ACK:
        out.type = LocalType::Ack;
        return true;
      case MSG_REJECT:
        out.type = LocalType::Reject;
        return true;
      case MSG_IDENTITY_REQUEST:
        out.type = LocalType::IdentityRequest;
        return true;
      default:
        return false;
    }
  }
  if (d[0] == MSG_RM_STATUS && frame.len >= 13) {
    out.type = LocalType::RmStatus;
    read_rm_state(d + 1, out.rm);
    out.sid = d[11];
    out.rm_status_d12 = d[12];
    return true;
  }
  if (d[0] == MSG_RM_REPORT) {
    return parse_rm_report(frame, out);
  }
  return false;
}

bool is_host_request(RadioType type) {
  switch (type) {
    case RadioType::FaultDetailsRequest:
    case RadioType::DiagnoseRequest:
    case RadioType::IdRequest:
    case RadioType::FaultEventsRequest:
    case RadioType::BatteryRequest:
      return true;
    default:
      return false;
  }
}

bool parse_radio(const Frame &frame, RadioMessage &out) {
  out = RadioMessage{};
  if (frame.status != FrameStatus::Complete || frame.len == 0) {
    return false;
  }
  const uint8_t *d = frame.data;
  const RadioLayout *layout = nullptr;
  for (const RadioLayout &candidate : RADIO_LAYOUTS) {
    if (candidate.code == d[0]) {
      layout = &candidate;
      break;
    }
  }
  if (layout == nullptr) {
    return false;
  }
  // Anfragen tragen bei manchen Absendern ein zusätzliches Byte vor SID und Sequenznummer.
  const uint8_t max_len = is_host_request(layout->type) ? layout->len + 1 : layout->len;
  if (frame.len < layout->len || frame.len > max_len) {
    return false;
  }
  out.type = layout->type;
  out.code = d[0];
  out.sid = d[frame.len - 2];
  out.seq = d[frame.len - 1];
  switch (out.type) {
    case RadioType::Alarm:
    case RadioType::Silence:
      set_device(out, d + 1);
      set_class(out, d[4]);
      out.value = d[5];
      break;
    case RadioType::AlarmStop:
      set_device(out, d + 1);
      set_class(out, d[4]);
      break;
    case RadioType::Test:
      set_device(out, d + 1);
      set_class(out, d[4]);
      out.value = d[5];
      set_model(out, d + 6);
      break;
    case RadioType::Fault:
    case RadioType::FaultDetailsRequest:
    case RadioType::FaultEventsRequest:
    case RadioType::BatteryRequest:
    case RadioType::UnknownD5:
      set_device(out, d + 1);
      set_model(out, d + 4);
      out.value = d[6];
      break;
    case RadioType::FaultDetails:
      set_device(out, d + 1);
      set_model(out, d + 4);
      out.value16 = static_cast<uint16_t>(d[6] | (d[7] << 8));
      break;
    case RadioType::DiagnoseRequest:
      out.value = d[7];
      break;
    case RadioType::DiagnoseResult:
      set_device(out, d + 1);
      set_class(out, d[4]);
      set_model(out, d + 5);
      out.value = d[7];
      break;
    case RadioType::IdRequest:
      break;
    case RadioType::IdInfo:
      set_device(out, d + 1);
      set_class(out, d[4]);
      set_model(out, d + 5);
      break;
    case RadioType::FaultEvents:
      set_device(out, d + 1);
      out.value = d[4];
      std::memcpy(out.events, d + 5, 3);
      break;
    case RadioType::Battery:
      set_device(out, d + 1);
      out.value = d[4];
      out.value16 = static_cast<uint16_t>(d[5] | (d[6] << 8));
      break;
    case RadioType::None:
      return false;
  }
  return true;
}

uint32_t rm_battery_mv(uint8_t bvnl) { return 1048576u / (static_cast<uint32_t>(bvnl) + 285u); }

uint64_t read_sid_map(const uint8_t *bytes) {
  uint64_t map = 0;
  for (int index = 0; index < 8; ++index) {
    map |= static_cast<uint64_t>(bytes[index]) << (8 * index);
  }
  return map;
}

int count_sids(uint64_t map) {
  int count = 0;
  while (map != 0) {
    map &= map - 1;
    ++count;
  }
  return count;
}

void format_device_id(const uint8_t *id, char *out, size_t out_len) {
  std::snprintf(out, out_len, "%02X%02X%02X", id[2], id[1], id[0]);
}

bool parse_device_id(const char *text, uint8_t *id) {
  if (text == nullptr || std::strlen(text) != 6) {
    return false;
  }
  uint8_t display[3];
  for (int index = 0; index < 3; ++index) {
    const int hi = hex_value(text[2 * index]);
    const int lo = hex_value(text[2 * index + 1]);
    if (hi < 0 || lo < 0) {
      return false;
    }
    display[index] = static_cast<uint8_t>((hi << 4) | lo);
  }
  id[0] = display[2];
  id[1] = display[1];
  id[2] = display[0];
  return true;
}

size_t parse_hex_bytes(const char *text, uint8_t *out, size_t out_cap) {
  if (text == nullptr) {
    return 0;
  }
  size_t count = 0;
  for (const char *pos = text; *pos != '\0';) {
    if (*pos == ' ') {
      ++pos;
      continue;
    }
    const int hi = hex_value(pos[0]);
    const int lo = hi < 0 ? -1 : hex_value(pos[1]);
    if (lo < 0 || count == out_cap) {
      return 0;
    }
    out[count++] = static_cast<uint8_t>((hi << 4) | lo);
    pos += 2;
  }
  return count;
}

}  // namespace wisafe2
}  // namespace esphome
