#include "node_view.h"

#include "catalog.h"

#include <cstdio>
#include <cstring>
#include <ctime>

namespace esphome {
namespace wisafe2 {
namespace {

constexpr uint8_t FLAG_FAULT = 0x02;
constexpr uint8_t FLAG_ON_SOCKET = 0x04;
constexpr uint8_t FLAG_BATTERY = 0x08;
constexpr uint8_t FLAG_RM_BATTERY = 0x20;

void append(char *out, size_t out_len, const char *text) {
  const size_t used = std::strlen(out);
  if (used + 1 < out_len) {
    std::snprintf(out + used, out_len - used, "%s", text);
  }
}

void start(char *out, size_t out_len) {
  if (out_len != 0) {
    out[0] = '\0';
  }
}

Tri from_bits(const Node &node, uint8_t flag, uint16_t detail_mask) {
  if (!node.has_fault_flags && !node.has_fault_details) {
    return Tri::Unknown;
  }
  const bool by_flag = node.has_fault_flags && (node.fault_flags & flag) != 0;
  const bool by_detail = node.has_fault_details && (node.fault_details & detail_mask) != 0;
  return by_flag || by_detail ? Tri::On : Tri::Off;
}

}  // namespace

Tri node_alarm(const Node &node) {
  switch (node.alarm) {
    case AlarmState::Active:
      return Tri::On;
    case AlarmState::Ended:
      return Tri::Off;
    case AlarmState::Unknown:
      break;
  }
  return Tri::Unknown;
}

Tri node_fault(const Node &node) { return from_bits(node, FLAG_FAULT, 0xFFFF); }

Tri node_socket_problem(const Node &node) {
  if (!node.has_fault_flags) {
    return Tri::Unknown;
  }
  return (node.fault_flags & FLAG_ON_SOCKET) != 0 ? Tri::Off : Tri::On;
}

Tri node_battery_low(const Node &node) { return from_bits(node, FLAG_BATTERY, battery_low_detail_mask(node.model)); }

Tri node_rm_battery_low(const Node &node) {
  return from_bits(node, FLAG_RM_BATTERY, rm_battery_low_detail_mask(node.model));
}

bool node_online(const Node &node) { return node.in_network && node.reach != Reach::Unreachable; }

void describe_model(const Node &node, char *out, size_t out_len) {
  start(out, out_len);
  char text[24];
  const char *name = node.has_model() ? model_name(node.model) : nullptr;
  if (name != nullptr) {
    append(out, out_len, name);
  } else if (node.has_model()) {
    std::snprintf(text, sizeof(text), "Modell %u", node.model);
    append(out, out_len, text);
  } else {
    append(out, out_len, "Modell unbekannt");
  }
  if (node.has_device()) {
    char id[8];
    format_device_id(node.device_id, id, sizeof(id));
    std::snprintf(text, sizeof(text), ", %s", id);
    append(out, out_len, text);
  }
}

void describe_fault_details(uint16_t model, uint16_t fault_details, char *out, size_t out_len) {
  start(out, out_len);
  if (fault_details == 0) {
    append(out, out_len, "keine Fehler");
    return;
  }
  bool first = true;
  for (int bit = 0; bit < 16; ++bit) {
    if ((fault_details & (1u << bit)) == 0) {
      continue;
    }
    if (!first) {
      append(out, out_len, ", ");
    }
    first = false;
    const char *name = fault_detail_name(model, bit);
    if (name != nullptr) {
      append(out, out_len, name);
    } else {
      char text[8];
      std::snprintf(text, sizeof(text), "Bit %d", bit);
      append(out, out_len, text);
    }
  }
}

bool epoch_valid(int64_t epoch) { return epoch >= 1577836800; }

void describe_test(uint8_t result, int64_t epoch, char *out, size_t out_len) {
  start(out, out_len);
  append(out, out_len, result == 0x01 ? "bestanden" : "fehlgeschlagen");
  if (!epoch_valid(epoch)) {
    return;
  }
  const time_t value = static_cast<time_t>(epoch);
  struct tm local {};
  char text[24];
  if (localtime_r(&value, &local) != nullptr && std::strftime(text, sizeof(text), " am %d.%m. %H:%M", &local) != 0) {
    append(out, out_len, text);
  }
}

void format_timestamp(int64_t epoch, char *out, size_t out_len) {
  start(out, out_len);
  const time_t value = static_cast<time_t>(epoch);
  struct tm utc {};
  if (gmtime_r(&value, &utc) == nullptr || std::strftime(out, out_len, "%Y-%m-%dT%H:%M:%SZ", &utc) == 0) {
    start(out, out_len);
  }
}

const char *event_type(RadioType type) {
  switch (type) {
    case RadioType::Alarm:
      return "alarm";
    case RadioType::AlarmStop:
      return "alarm_ende";
    case RadioType::Silence:
      return "stummschaltung";
    case RadioType::Test:
      return "test";
    case RadioType::Fault:
      return "fehlerbericht";
    default:
      return nullptr;
  }
}

bool is_test_request(const RadioMessage &msg) {
  return msg.type == RadioType::Test && msg.has_device && is_gateway_id(msg.device_id);
}

void describe_event(const RadioMessage &msg, const char *name, char *out, size_t out_len) {
  start(out, out_len);
  if (is_test_request(msg)) {
    char text[48];
    std::snprintf(text, sizeof(text), "Testanforderung des W2-MM: SID %u", msg.sid);
    append(out, out_len, text);
    return;
  }
  const char *what = nullptr;
  switch (msg.type) {
    case RadioType::Alarm:
      what = "Alarm";
      break;
    case RadioType::AlarmStop:
      what = "Alarmende";
      break;
    case RadioType::Silence:
      what = "Stummschaltung";
      break;
    case RadioType::Test:
      what = msg.value == 0x01 ? "Test bestanden" : "Test fehlgeschlagen";
      break;
    case RadioType::Fault:
      what = "Fehlerbericht";
      break;
    default:
      return;
  }
  char text[64];
  if ((name == nullptr || name[0] == '\0') && msg.has_device && is_gateway_id(msg.device_id)) {
    name = "Gateway";
  }
  if (name != nullptr && name[0] != '\0') {
    std::snprintf(text, sizeof(text), "%s: SID %u (%s)", what, msg.sid, name);
  } else {
    std::snprintf(text, sizeof(text), "%s: SID %u", what, msg.sid);
  }
  append(out, out_len, text);
  const char *detail = nullptr;
  if (msg.type == RadioType::Alarm) {
    detail = class_name(msg.device_class);
  } else if (msg.type == RadioType::Silence) {
    detail = silence_class_name(msg.device_class);
  }
  if (detail != nullptr) {
    append(out, out_len, ", ");
    append(out, out_len, detail);
  }
  if (msg.type == RadioType::Fault) {
    append(out, out_len, (msg.value & FLAG_ON_SOCKET) != 0 ? ", auf Sockel" : ", abgenommen");
    for (unsigned bit = 0x02; bit <= 0x80; bit <<= 1) {
      if (bit == FLAG_ON_SOCKET || (msg.value & bit) == 0) {
        continue;
      }
      append(out, out_len, ", ");
      append(out, out_len, fault_flag_name(static_cast<uint8_t>(bit)));
    }
  }
}

bool node_assignable(const Node &node) {
  return node.has_device() && !node.is_own() && !node.is_gateway() && (node.flags & NODE_TYPE_UNKNOWN) == 0;
}

const char *alarm_device_class(uint8_t device_class) {
  switch (device_class) {
    case 0x41:
      return "carbon_monoxide";
    case 0x81:
    case 0x86:
      return "smoke";
    case 0x82:
    case 0x87:
      return "heat";
    default:
      return nullptr;
  }
}

bool template_by_device(const Registry &registry, const Node &node) {
  return node.has_device() && registry.find_device(node.device_id) == &node;
}

void template_name(const Node &node, bool by_device, char *out, size_t out_len) {
  start(out, out_len);
  char text[32];
  if (!by_device) {
    std::snprintf(text, sizeof(text), "Melder SID %u", node.sid);
    append(out, out_len, text);
    return;
  }
  char id[8];
  format_device_id(node.device_id, id, sizeof(id));
  const char *name = node.has_model() ? model_name(node.model) : nullptr;
  std::snprintf(text, sizeof(text), "%s %s", name != nullptr ? name : "Melder", id);
  append(out, out_len, text);
}

void template_device_key(const Node &node, bool by_device, char *out, size_t out_len) {
  start(out, out_len);
  char text[24];
  if (!by_device) {
    std::snprintf(text, sizeof(text), "alarm_sid_%u", node.sid);
    append(out, out_len, text);
    return;
  }
  char id[8];
  format_device_id(node.device_id, id, sizeof(id));
  std::snprintf(text, sizeof(text), "alarm_%s", id);
  append(out, out_len, text);
}

void describe_unassigned(const Registry &registry, uint64_t assigned, char *out, size_t out_len) {
  start(out, out_len);
  static const char *const MORE = ", …";
  bool first = true;
  for (uint8_t sid = 0; sid <= MAX_SID; ++sid) {
    const Node *node = registry.find(sid);
    if (node == nullptr || !node_assignable(*node) || ((assigned >> sid) & 1u) != 0) {
      continue;
    }
    char text[12];
    std::snprintf(text, sizeof(text), first ? "SID %u" : ", %u", sid);
    if (std::strlen(out) + std::strlen(text) + std::strlen(MORE) + 1 > out_len) {
      append(out, out_len, MORE);
      return;
    }
    append(out, out_len, text);
    first = false;
  }
  if (first) {
    append(out, out_len, "keine");
  }
}

}  // namespace wisafe2
}  // namespace esphome
