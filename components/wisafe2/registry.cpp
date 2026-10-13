#include "registry.h"

#include "catalog.h"

#include <cstdarg>
#include <cstdio>
#include <cstring>

namespace esphome {
namespace wisafe2 {
namespace {

constexpr uint8_t MAGIC[4] = {'W', '2', 'N', 'R'};
constexpr uint8_t FREE_SLOT = 0xFF;

bool same_id(const uint8_t *a, const uint8_t *b) { return std::memcmp(a, b, 3) == 0; }

bool is_zero_id(const uint8_t *id) { return id[0] == 0 && id[1] == 0 && id[2] == 0; }

bool persisted(const Node &node) { return node.has_device() || node.flags != 0; }

class Appender {
 public:
  Appender(char *out, size_t len) : out_(out), len_(len) {
    if (len_ != 0) {
      out_[0] = '\0';
    }
  }

  void add(const char *format, ...) __attribute__((format(printf, 2, 3))) {
    if (this->used_ + 1 >= this->len_) {
      return;
    }
    va_list args;
    va_start(args, format);
    const int written = std::vsnprintf(this->out_ + this->used_, this->len_ - this->used_, format, args);
    va_end(args);
    if (written > 0) {
      this->used_ += static_cast<size_t>(written);
      if (this->used_ >= this->len_) {
        this->used_ = this->len_ - 1;
      }
    }
  }

 private:
  char *out_;
  size_t len_;
  size_t used_{0};
};

}  // namespace

bool Node::is_detector() const {
  if (!this->has_device() || !this->has_model() || this->is_gateway() || this->is_own() ||
      (this->flags & NODE_TYPE_UNKNOWN) != 0) {
    return false;
  }
  switch (this->model) {
    case MODEL_W2_TSL:
    case MODEL_IFG_100:
    case MODEL_IFG_200:
      return false;
    default:
      return true;
  }
}

bool Node::has_battery_report() const { return this->is_detector() && this->model != MODEL_WHT_630; }

bool Node::has_fault_events_support() const {
  return this->is_detector() && (this->model == MODEL_WST_630 || this->model == MODEL_ST_630_DE);
}

bool Node::complete() const {
  if (this->is_own()) {
    return true;
  }
  return this->has_device() && (this->has_class() || (this->flags & (NODE_GATEWAY | NODE_TYPE_UNKNOWN)) != 0);
}

const char *load_result_name(LoadResult result) {
  switch (result) {
    case LoadResult::Ok:
      return "ok";
    case LoadResult::BadLength:
      return "Länge falsch";
    case LoadResult::BadMagic:
      return "Kennung fehlt";
    case LoadResult::BadVersion:
      return "Formatversion unbekannt";
    case LoadResult::BadCrc:
      return "CRC falsch";
    case LoadResult::BadContent:
      return "Inhalt ungültig";
  }
  return "?";
}

uint16_t crc16_ccitt(const uint8_t *data, size_t len) {
  uint16_t crc = 0xFFFF;
  for (size_t index = 0; index < len; ++index) {
    crc ^= static_cast<uint16_t>(data[index]) << 8;
    for (int bit = 0; bit < 8; ++bit) {
      crc = (crc & 0x8000) != 0 ? static_cast<uint16_t>((crc << 1) ^ 0x1021) : static_cast<uint16_t>(crc << 1);
    }
  }
  return crc;
}

Node *Registry::find(uint8_t sid) {
  for (size_t index = 0; index < this->count_; ++index) {
    if (this->nodes_[index].sid == sid) {
      return &this->nodes_[index];
    }
  }
  return nullptr;
}

const Node *Registry::find(uint8_t sid) const { return const_cast<Registry *>(this)->find(sid); }

const Node *Registry::find_device(const uint8_t *device_id) const {
  if (device_id == nullptr || is_zero_id(device_id)) {
    return nullptr;
  }
  // Ein RM behält die gelernte Geräte-ID, auch wenn es keinen Melder mehr trägt. Dann führt eine zweite SID
  // dieselbe ID; der Eintrag mit Modell ist der Melder.
  const Node *found = nullptr;
  for (size_t index = 0; index < this->count_; ++index) {
    const Node &node = this->nodes_[index];
    if (!same_id(node.device_id, device_id)) {
      continue;
    }
    if (node.has_model()) {
      return &node;
    }
    if (found == nullptr) {
      found = &node;
    }
  }
  return found;
}

const Node *Registry::resolve(uint8_t sid, const uint8_t *device_id) const {
  if (device_id == nullptr || is_zero_id(device_id)) {
    return this->find(sid);
  }
  const Node *node = this->find_device(device_id);
  if (node != nullptr) {
    return node;
  }
  node = this->find(sid);
  return node != nullptr && !node->has_device() ? node : nullptr;
}

AlarmState Registry::network_alarm() const {
  AlarmState state = AlarmState::Unknown;
  for (size_t index = 0; index < this->count_; ++index) {
    const AlarmState alarm = this->nodes_[index].alarm;
    if (alarm == AlarmState::Active) {
      return AlarmState::Active;
    }
    if (alarm == AlarmState::Ended) {
      state = AlarmState::Ended;
    }
  }
  return state;
}

bool Registry::any_silenced() const {
  for (size_t index = 0; index < this->count_; ++index) {
    if (this->nodes_[index].silenced) {
      return true;
    }
  }
  return false;
}

Node *Registry::get_or_add(uint8_t sid, uint8_t &changes) {
  if (sid > MAX_SID) {
    return nullptr;
  }
  Node *node = this->find(sid);
  if (node != nullptr) {
    return node;
  }
  if (this->count_ >= REGISTRY_SLOTS) {
    return nullptr;
  }
  node = &this->nodes_[this->count_++];
  *node = Node{};
  node->sid = sid;
  changes |= CHANGE_NEW;
  return node;
}

bool Registry::known(uint8_t sid) const {
  const Node *node = this->find(sid);
  return node != nullptr && node->complete();
}

uint64_t Registry::members() const {
  uint64_t map = 0;
  for (size_t index = 0; index < this->count_; ++index) {
    if (this->nodes_[index].in_network) {
      map |= uint64_t{1} << this->nodes_[index].sid;
    }
  }
  return map;
}

void Registry::clear() {
  for (Node &node : this->nodes_) {
    node = Node{};
  }
  this->count_ = 0;
}

void Registry::seen_(Node &node, uint32_t now_ms, uint8_t &changes) {
  node.seen = true;
  node.last_seen_ms = now_ms;
  node.missed = 0;
  if (node.reach != Reach::Reachable) {
    if (node.reach == Reach::Unreachable) {
      changes |= CHANGE_REACH;
    }
    node.reach = Reach::Reachable;
  }
  changes |= CHANGE_STATE;
}

void Registry::set_flag_(Node &node, uint8_t flag, bool on, uint8_t &changes) {
  const uint8_t flags = on ? (node.flags | flag) : (node.flags & ~flag);
  if (flags != node.flags) {
    node.flags = flags;
    changes |= CHANGE_IDENTITY;
  }
}

void Registry::set_device_(Node &node, const uint8_t *id, uint8_t &changes) {
  if (is_zero_id(id) || same_id(node.device_id, id)) {
    return;
  }
  if (node.has_device()) {
    // Das RM steckt jetzt in einem anderen Melder: Modell, Klasse und Melderwerte gehören zum alten Gerät.
    changes |= CHANGE_DEVICE;
    node.model = 0;
    node.device_class = 0;
    node.flags &= ~NODE_TYPE_UNKNOWN;
    node.has_battery = false;
    node.has_fault_flags = false;
    node.has_fault_details = false;
    node.has_fault_events = false;
    node.has_test = false;
  }
  std::memcpy(node.device_id, id, 3);
  changes |= CHANGE_IDENTITY;
  this->set_flag_(node, NODE_GATEWAY, is_gateway_id(id), changes);
}

void Registry::set_model_(Node &node, uint16_t model, uint8_t &changes) {
  if (model != 0 && model != node.model) {
    node.model = model;
    changes |= CHANGE_IDENTITY;
  }
}

void Registry::set_class_(Node &node, uint8_t device_class, uint8_t &changes) {
  if (device_class != 0 && device_class != node.device_class) {
    node.device_class = device_class;
    changes |= CHANGE_IDENTITY;
  }
  if (device_class != 0) {
    this->set_flag_(node, NODE_TYPE_UNKNOWN, false, changes);
  }
}

void Registry::set_unreachable_(Node &node, uint8_t &changes) {
  if (node.reach != Reach::Unreachable) {
    node.reach = Reach::Unreachable;
    changes |= CHANGE_REACH | CHANGE_STATE;
  }
  node.missed = 0;
}

uint8_t Registry::apply_membership_(uint64_t map) {
  uint8_t changes = 0;
  for (uint8_t sid = 0; sid <= MAX_SID; ++sid) {
    if ((map >> sid) & 1u) {
      this->get_or_add(sid, changes);
    }
  }
  for (size_t index = 0; index < this->count_; ++index) {
    Node &node = this->nodes_[index];
    const bool member = ((map >> node.sid) & 1u) != 0;
    if (node.in_network != member) {
      node.in_network = member;
      changes |= CHANGE_STATE;
    }
  }
  return changes;
}

uint8_t Registry::apply(const RadioMessage &msg, uint32_t now_ms) {
  uint8_t changes = 0;
  if (msg.type == RadioType::None || msg.sid > MAX_SID) {
    return changes;
  }
  if (is_host_request(msg.type)) {
    // Anfragen tragen das Zielgerät. Der Absender kann ein Dienstgerät außerhalb der SID-Map sein (P8).
    Node *sender = this->find(msg.sid);
    if (sender != nullptr) {
      this->seen_(*sender, now_ms, changes);
    }
    return changes;
  }
  if (msg.has_device && is_gateway_id(msg.device_id) && this->find(msg.sid) == nullptr) {
    // Auch das W2-MM sendet mit der Gateway-ID, etwa 70 von SID 15, und steht in keiner SID-Map (P8, P18).
    return changes;
  }
  Node *node = this->get_or_add(msg.sid, changes);
  if (node == nullptr) {
    return changes;
  }
  this->seen_(*node, now_ms, changes);
  if (msg.has_device) {
    this->set_device_(*node, msg.device_id, changes);
  }
  if (msg.has_model) {
    this->set_model_(*node, msg.model, changes);
  }
  if ((msg.type == RadioType::Fault || msg.type == RadioType::FaultDetails) && msg.model != 0) {
    // 71 und 73 mit Modell kommen vom Melder selbst; ein ausgebliebenes C4 ging dann nur verloren (P15).
    this->set_flag_(*node, NODE_TYPE_UNKNOWN, false, changes);
  }
  switch (msg.type) {
    case RadioType::Test:
      this->set_class_(*node, msg.device_class, changes);
      node->has_test = true;
      node->test_result = msg.value;
      node->test_ms = now_ms;
      break;
    case RadioType::IdInfo:
    case RadioType::DiagnoseResult:
      this->set_class_(*node, msg.device_class, changes);
      break;
    case RadioType::Alarm:
      node->alarm = AlarmState::Active;
      node->alarm_class = msg.device_class;
      node->silenced = false;
      break;
    case RadioType::AlarmStop:
      node->alarm = AlarmState::Ended;
      node->silenced = false;
      break;
    case RadioType::Silence:
      // 61 beendet keinen Alarm.
      node->silenced = true;
      break;
    case RadioType::Fault:
      node->has_fault_flags = true;
      node->fault_flags = msg.value;
      break;
    case RadioType::FaultDetails:
      node->has_fault_details = true;
      node->fault_details = msg.value16;
      break;
    case RadioType::FaultEvents:
      node->has_fault_events = true;
      std::memcpy(node->fault_events, msg.events, 3);
      break;
    case RadioType::Battery:
      if (node->model != MODEL_WHT_630) {
        node->has_battery = true;
        node->battery_mv = msg.value16;
      }
      break;
    default:
      break;
  }
  return changes;
}

uint8_t Registry::apply(const LocalMessage &msg, uint32_t now_ms) {
  uint8_t changes = 0;
  switch (msg.type) {
    case LocalType::LocalSidMap:
    case LocalType::NewNetworkMap:
      return this->apply_membership_(msg.map);
    case LocalType::MissingMap:
      for (size_t index = 0; index < this->count_; ++index) {
        Node &node = this->nodes_[index];
        if ((msg.map >> node.sid) & 1u) {
          this->set_unreachable_(node, changes);
        }
      }
      return changes;
    case LocalType::RmStatus:
    case LocalType::RemoteStatus: {
      if (msg.sid > MAX_SID) {
        return changes;
      }
      Node *node = this->get_or_add(msg.sid, changes);
      if (node == nullptr) {
        return changes;
      }
      if (msg.type == LocalType::RmStatus) {
        for (size_t index = 0; index < this->count_; ++index) {
          if (&this->nodes_[index] != node) {
            this->set_flag_(this->nodes_[index], NODE_OWN, false, changes);
          }
        }
        this->set_flag_(*node, NODE_OWN, true, changes);
      }
      this->seen_(*node, now_ms, changes);
      // Beim eigenen RM gilt die Identität aus der YAML (set_own); D2 liefert sie nur, solange keine bekannt ist.
      if (msg.type == LocalType::RemoteStatus || !node->has_device()) {
        this->set_device_(*node, msg.rm.device_id, changes);
      }
      node->has_rm = true;
      node->rm_bvnl = msg.rm.bvnl;
      node->rm_rssi = msg.rm.rssi;
      node->rm_firmware = msg.rm.firmware;
      node->rm_critical = msg.rm.critical;
      node->rm_radio_faults = msg.rm.radio_faults;
      return changes;
    }
    case LocalType::PresenceHeard: {
      // Beim Einlernen eines Gateways kam D4 09 00 01 02 03 mit SID-Feld 00.
      if (msg.sid > MAX_SID || is_zero_id(msg.device_id) || (msg.sid == 0 && is_gateway_id(msg.device_id))) {
        return changes;
      }
      Node *node = this->get_or_add(msg.sid, changes);
      if (node == nullptr) {
        return changes;
      }
      this->seen_(*node, now_ms, changes);
      this->set_device_(*node, msg.device_id, changes);
      return changes;
    }
    default:
      return changes;
  }
}

uint8_t Registry::set_own(uint8_t sid, const uint8_t *device_id, uint16_t model, uint8_t device_class) {
  uint8_t changes = 0;
  Node *node = this->get_or_add(sid, changes);
  if (node == nullptr) {
    return changes;
  }
  this->set_flag_(*node, NODE_OWN, true, changes);
  this->set_device_(*node, device_id, changes);
  this->set_model_(*node, model, changes);
  this->set_class_(*node, device_class, changes);
  return changes;
}

uint8_t Registry::on_query_result(uint8_t sid, uint8_t query, Outcome outcome) {
  uint8_t changes = 0;
  Node *node = this->find(sid);
  if (node == nullptr) {
    return changes;
  }
  switch (outcome) {
    case Outcome::Unreachable:
      this->set_unreachable_(*node, changes);
      break;
    case Outcome::Timeout:
      if (query == QUERY_IDENTITY) {
        // RMs ohne Melder beantworten C3 nicht. Das zählt nicht als ausgebliebene Antwort.
        if (node->has_device() && !node->has_class() && !node->is_gateway()) {
          this->set_flag_(*node, NODE_TYPE_UNKNOWN, true, changes);
        }
        break;
      }
      node->missed++;
      changes |= CHANGE_STATE;
      if (node->missed >= 2) {
        this->set_unreachable_(*node, changes);
      }
      break;
    default:
      break;
  }
  return changes;
}

size_t Registry::serialize(uint8_t *out, size_t out_len) const {
  if (out == nullptr || out_len < REGISTRY_FILE_LEN) {
    return 0;
  }
  std::memset(out, 0, REGISTRY_FILE_LEN);
  std::memcpy(out, MAGIC, sizeof(MAGIC));
  out[4] = REGISTRY_FORMAT_VERSION;
  uint8_t *slots = out + REGISTRY_HEADER_LEN;
  uint8_t used = 0;
  for (size_t index = 0; index < this->count_; ++index) {
    const Node &node = this->nodes_[index];
    if (!persisted(node)) {
      continue;
    }
    uint8_t *slot = slots + used * REGISTRY_SLOT_LEN;
    slot[0] = node.sid;
    slot[1] = node.flags;
    std::memcpy(slot + 2, node.device_id, 3);
    write_model(node.model, slot + 5);
    slot[7] = node.device_class;
    used++;
  }
  for (size_t index = used; index < REGISTRY_SLOTS; ++index) {
    slots[index * REGISTRY_SLOT_LEN] = FREE_SLOT;
  }
  out[5] = used;
  const uint16_t crc = crc16_ccitt(slots, REGISTRY_SLOTS * REGISTRY_SLOT_LEN);
  out[6] = static_cast<uint8_t>(crc & 0xFF);
  out[7] = static_cast<uint8_t>(crc >> 8);
  return REGISTRY_FILE_LEN;
}

LoadResult Registry::deserialize(const uint8_t *data, size_t len) {
  this->clear();
  if (data == nullptr || len != REGISTRY_FILE_LEN) {
    return LoadResult::BadLength;
  }
  if (std::memcmp(data, MAGIC, sizeof(MAGIC)) != 0) {
    return LoadResult::BadMagic;
  }
  if (data[4] != REGISTRY_FORMAT_VERSION) {
    return LoadResult::BadVersion;
  }
  const uint8_t *slots = data + REGISTRY_HEADER_LEN;
  const uint16_t crc = crc16_ccitt(slots, REGISTRY_SLOTS * REGISTRY_SLOT_LEN);
  if (data[6] != (crc & 0xFF) || data[7] != (crc >> 8)) {
    return LoadResult::BadCrc;
  }
  for (size_t index = 0; index < REGISTRY_SLOTS; ++index) {
    const uint8_t *slot = slots + index * REGISTRY_SLOT_LEN;
    if (slot[0] == FREE_SLOT) {
      continue;
    }
    if (slot[0] > MAX_SID || this->find(slot[0]) != nullptr) {
      this->clear();
      return LoadResult::BadContent;
    }
    Node &node = this->nodes_[this->count_++];
    node.sid = slot[0];
    node.flags = slot[1];
    std::memcpy(node.device_id, slot + 2, 3);
    node.model = read_model(slot + 5);
    node.device_class = slot[7];
  }
  if (this->count_ != data[5]) {
    this->clear();
    return LoadResult::BadContent;
  }
  return LoadResult::Ok;
}

void describe_node(const Node &node, uint32_t now_ms, char *out, size_t out_len) {
  Appender text(out, out_len);
  text.add("SID %u:", node.sid);
  if (node.has_device()) {
    char id[8];
    format_device_id(node.device_id, id, sizeof(id));
    text.add(" %s", id);
  } else {
    text.add(" Gerät unbekannt");
  }
  if (node.has_model()) {
    const char *name = model_name(node.model);
    if (name != nullptr) {
      text.add(" %s", name);
    } else {
      text.add(" Modell %u", node.model);
    }
  }
  if (node.has_class()) {
    const char *name = class_name(node.device_class);
    if (name != nullptr) {
      text.add(", %s", name);
    } else {
      text.add(", Klasse %02X", node.device_class);
    }
  }
  if (node.is_own()) {
    text.add(", eigenes RM");
  }
  if (node.is_gateway()) {
    text.add(", Gateway");
  }
  if ((node.flags & NODE_TYPE_UNKNOWN) != 0) {
    text.add(", RM ohne Melder");
  }
  text.add(node.in_network ? ", im Netz" : ", außerhalb der SID-Map");
  switch (node.reach) {
    case Reach::Reachable:
      text.add(", erreichbar");
      break;
    case Reach::Unreachable:
      text.add(", nicht erreichbar");
      break;
    case Reach::Unknown:
      break;
  }
  if (node.has_battery) {
    text.add(", Batterie %u mV", node.battery_mv);
  }
  if (node.has_rm) {
    text.add(", RM %u mV, RSSI %u", static_cast<unsigned>(rm_battery_mv(node.rm_bvnl)), node.rm_rssi);
    if (node.rm_critical != 0) {
      text.add(", kritisch %02X", node.rm_critical);
    }
    if (node.rm_radio_faults != 0) {
      text.add(", Funkfehler %u", node.rm_radio_faults);
    }
  }
  if (node.has_fault_flags) {
    text.add(", Flags %02X %s", node.fault_flags, (node.fault_flags & 0x04) != 0 ? "auf Sockel" : "abgenommen");
  }
  if (node.has_fault_details) {
    text.add(", FS %04X", node.fault_details);
  }
  if (node.has_fault_events) {
    text.add(", Ereignisse %02X %02X %02X", node.fault_events[0], node.fault_events[1], node.fault_events[2]);
  }
  if (node.has_test) {
    text.add(", Test %s", node.test_result == 0x01 ? "ok" : "Fehler");
  }
  if (node.alarm == AlarmState::Active) {
    text.add(", ALARM");
  }
  if (node.silenced) {
    text.add(", stummgeschaltet");
  }
  if (node.seen) {
    text.add(", zuletzt vor %u s", static_cast<unsigned>((now_ms - node.last_seen_ms) / 1000u));
  }
}

}  // namespace wisafe2
}  // namespace esphome
