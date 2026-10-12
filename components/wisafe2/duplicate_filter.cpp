#include "duplicate_filter.h"

namespace esphome {
namespace wisafe2 {

bool DuplicateFilter::seen(uint8_t sid, uint8_t code, uint8_t seq, uint32_t now_ms) {
  for (Entry &entry : this->entries_) {
    if (!entry.used || now_ms - entry.at_ms >= WINDOW_MS) {
      entry.used = false;
      continue;
    }
    if (entry.sid == sid && entry.code == code && entry.seq == seq) {
      return true;
    }
  }
  this->entries_[this->next_] = Entry{true, sid, code, seq, now_ms};
  this->next_ = (this->next_ + 1) % ENTRIES;
  return false;
}

void DuplicateFilter::clear() {
  for (Entry &entry : this->entries_) {
    entry.used = false;
  }
  this->next_ = 0;
}

}  // namespace wisafe2
}  // namespace esphome
