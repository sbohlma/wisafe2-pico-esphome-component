#include "node_cache.h"

#include <cstring>

namespace esphome {
namespace wisafe2 {

void NodeCache::update(const RadioMessage &msg) {
  // Anfragen tragen das Zielgerät, nicht den Absender.
  if (msg.type == RadioType::None || is_host_request(msg.type) || msg.sid > MAX_SID) {
    return;
  }
  NodeInfo &node = this->nodes_[msg.sid];
  if (msg.has_device) {
    node.has_device = true;
    std::memcpy(node.device_id, msg.device_id, 3);
  }
  if (msg.has_model) {
    node.has_model = true;
    node.model = msg.model;
  }
  if (msg.has_class) {
    node.has_class = true;
    node.device_class = msg.device_class;
  }
}

void NodeCache::update(const LocalMessage &msg) {
  const uint8_t *id = nullptr;
  if (msg.type == LocalType::RemoteStatus) {
    id = msg.rm.device_id;
  } else if (msg.type == LocalType::PresenceHeard) {
    id = msg.device_id;
  }
  if (id == nullptr || msg.sid > MAX_SID || (id[0] == 0 && id[1] == 0 && id[2] == 0)) {
    return;
  }
  NodeInfo &node = this->nodes_[msg.sid];
  node.has_device = true;
  std::memcpy(node.device_id, id, 3);
}

const NodeInfo *NodeCache::get(uint8_t sid) const { return sid <= MAX_SID ? &this->nodes_[sid] : nullptr; }

void NodeCache::clear() {
  for (NodeInfo &node : this->nodes_) {
    node = NodeInfo{};
  }
}

}  // namespace wisafe2
}  // namespace esphome
