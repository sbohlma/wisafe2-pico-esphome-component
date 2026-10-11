#include "messages.h"

#include <cstdio>
#include <cstring>

namespace esphome {
namespace wisafe2 {
namespace {

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

}  // namespace wisafe2
}  // namespace esphome
