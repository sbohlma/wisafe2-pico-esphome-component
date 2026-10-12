#pragma once

#include "messages.h"

#include <cstdint>

namespace esphome {
namespace wisafe2 {

struct NodeInfo {
  bool has_device{false};
  uint8_t device_id[3]{};
  bool has_model{false};
  uint16_t model{0};
  bool has_class{false};
  uint8_t device_class{0};
};

// Geräte-ID, Modell und Klasse je SID aus gehörten Nachrichten, nur im RAM. Damit brauchen Abfragen wie C7
// nur die SID, sobald der Knoten einmal mit Geräte-ID und Modell gehört wurde. Stufe 2 ersetzt das durch das
// Melderregister.
class NodeCache {
 public:
  void update(const RadioMessage &msg);
  void update(const LocalMessage &msg);
  // nullptr für SIDs über 63.
  const NodeInfo *get(uint8_t sid) const;
  void clear();

 protected:
  NodeInfo nodes_[MAX_SID + 1]{};
};

}  // namespace wisafe2
}  // namespace esphome
