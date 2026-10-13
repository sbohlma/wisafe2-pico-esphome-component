#include "discovery.h"

namespace esphome {
namespace wisafe2 {
namespace {

constexpr uint8_t QUERY_ORDER[] = {QUERY_LOCATE,  QUERY_REMOTE_STATUS, QUERY_IDENTITY,
                                   QUERY_BATTERY, QUERY_FAULT_DETAILS, QUERY_FAULT_EVENTS};
constexpr uint8_t QUERY_STATUS_ANY = QUERY_LOCATE | QUERY_REMOTE_STATUS;

// QUERY_LOCATE sendet denselben Befehl wie QUERY_REMOTE_STATUS.
uint8_t command_of(uint8_t query) { return query == QUERY_LOCATE ? QUERY_REMOTE_STATUS : query; }

bool due(uint32_t now_ms, uint32_t at_ms) { return static_cast<int32_t>(now_ms - at_ms) >= 0; }

}  // namespace

uint8_t query_for_payload(const uint8_t *payload, size_t len) {
  if (payload == nullptr || len == 0) {
    return 0;
  }
  switch (payload[0]) {
    case 0xD3:
      if (len >= 4 && payload[1] == 0x06) {
        return payload[3] == 0x00 ? QUERY_REMOTE_STATUS : payload[3] == 0x01 ? QUERY_IDENTITY : 0;
      }
      return 0;
    case 0xC7:
      return QUERY_BATTERY;
    case 0x72:
      return QUERY_FAULT_DETAILS;
    case 0xC5:
      return QUERY_FAULT_EVENTS;
    default:
      return 0;
  }
}

const char *query_name(uint8_t query) {
  switch (query) {
    case QUERY_LOCATE:
    case QUERY_REMOTE_STATUS:
      return "Remote-Status";
    case QUERY_IDENTITY:
      return "ID-Information";
    case QUERY_BATTERY:
      return "Batterie";
    case QUERY_FAULT_DETAILS:
      return "Fehlerdetails";
    case QUERY_FAULT_EVENTS:
      return "Fehlerereignisse";
    default:
      return "?";
  }
}

void Discovery::request(Scope scope) {
  if (scope == Scope::Identify) {
    this->want_identify_ = true;
  } else {
    this->want_details_ = true;
  }
  this->waiting_ = true;
}

const char *Discovery::campaign_name() const {
  if (this->campaign_identify_ && this->campaign_details_) {
    return "Erkundung und Netzdetails";
  }
  return this->campaign_details_ ? "Netzdetails" : "Erkundung";
}

int Discovery::on_map(uint64_t map, const Registry &registry, uint8_t own_sid) {
  if (!this->waiting_) {
    return 0;
  }
  this->waiting_ = false;
  const bool identify = this->want_identify_;
  const bool details = this->want_details_;
  this->want_identify_ = false;
  this->want_details_ = false;
  uint64_t targets = 0;
  bool identifying = false;
  for (uint8_t sid = 0; sid <= MAX_SID; ++sid) {
    if (((map >> sid) & 1u) == 0 || sid == own_sid) {
      continue;
    }
    const Node *node = registry.find(sid);
    const bool unreachable = node != nullptr && node->reach == Reach::Unreachable;
    uint8_t queries = 0;
    if (identify && !registry.known(sid)) {
      queries |= QUERY_IDENTIFY;
      identifying = true;
    }
    if (identify && unreachable) {
      // Die Erkundung prüft unerreichbare SIDs erneut; erst eine Antwort gibt sie für die Netzdetails frei.
      queries |= QUERY_REMOTE_STATUS;
      identifying = true;
    }
    if (details && (!unreachable || identify)) {
      queries |= QUERY_DETAILS;
    }
    if (queries == 0) {
      continue;
    }
    targets |= uint64_t{1} << sid;
    this->wanted_[sid] |= queries;
  }
  const int count = count_sids(targets);
  if (count == 0) {
    return 0;
  }
  if (!this->campaign_) {
    this->campaign_ = true;
    this->campaign_identify_ = false;
    this->campaign_details_ = false;
    this->total_ = 0;
    this->index_ = 0;
  }
  this->campaign_identify_ |= identifying;
  this->campaign_details_ |= details;
  const uint64_t added = targets & ~this->campaign_pending_;
  this->campaign_pending_ |= targets;
  this->total_ += count_sids(added);
  return count;
}

void Discovery::want(uint8_t sid, uint8_t queries) {
  if (sid <= MAX_SID) {
    this->wanted_[sid] |= queries & (QUERY_ALL | QUERY_LOCATE);
  }
}

bool Discovery::has_work() const {
  for (uint8_t wanted : this->wanted_) {
    if (wanted != 0) {
      return true;
    }
  }
  return false;
}

bool Discovery::allowed_(const Registry &registry, uint8_t sid, uint8_t query) const {
  const Node *node = registry.find(sid);
  switch (query) {
    case QUERY_LOCATE:
      return node == nullptr || !node->has_device();
    case QUERY_REMOTE_STATUS:
      return true;
    case QUERY_IDENTITY:
      // Ein RM ohne Melder beantwortet C3 nicht; jede weitere Anfrage kostet nur das volle Zeitlimit.
      return node == nullptr || (!node->has_class() && !node->is_gateway() && !node->is_own() &&
                                 (node->flags & NODE_TYPE_UNKNOWN) == 0);
    case QUERY_BATTERY:
      return node != nullptr && node->has_battery_report() && node->reach != Reach::Unreachable;
    case QUERY_FAULT_DETAILS:
      return node != nullptr && node->is_detector() && node->reach != Reach::Unreachable;
    case QUERY_FAULT_EVENTS:
      return node != nullptr && node->has_fault_events_support() && node->reach != Reach::Unreachable;
    default:
      return false;
  }
}

bool Discovery::next(const Registry &registry, DiscoveryTask &task) {
  if (this->active_) {
    return false;
  }
  for (;;) {
    uint8_t sid = NO_SID;
    if (this->current_sid_ <= MAX_SID && this->wanted_[this->current_sid_] != 0) {
      sid = this->current_sid_;
    } else {
      for (uint8_t candidate = 0; candidate <= MAX_SID; ++candidate) {
        if (this->wanted_[candidate] != 0) {
          sid = candidate;
          break;
        }
      }
      if (sid == NO_SID) {
        this->current_sid_ = NO_SID;
        this->finish_if_done_();
        return false;
      }
      this->current_sid_ = sid;
    }
    // Auch für die zuletzt bearbeitete SID, wenn eine neue Runde mit ihr beginnt.
    const uint64_t bit = uint64_t{1} << sid;
    if ((this->campaign_pending_ & bit) != 0) {
      this->campaign_pending_ &= ~bit;
      this->index_++;
    }
    for (uint8_t query : QUERY_ORDER) {
      if ((this->wanted_[sid] & query) == 0) {
        continue;
      }
      this->wanted_[sid] &= static_cast<uint8_t>(~query);
      if (this->allowed_(registry, sid, query)) {
        // QUERY_LOCATE und QUERY_REMOTE_STATUS sind derselbe Befehl; er läuft je SID höchstens einmal.
        if ((query & QUERY_STATUS_ANY) != 0) {
          this->wanted_[sid] &= static_cast<uint8_t>(~QUERY_STATUS_ANY);
        }
        this->active_ = true;
        this->active_task_ = DiscoveryTask{sid, query};
        task = this->active_task_;
        return true;
      }
    }
  }
}

void Discovery::on_result(uint8_t sid, uint8_t query, Outcome outcome) {
  if (!this->active_ || this->active_task_.sid != sid || command_of(this->active_task_.query) != command_of(query)) {
    return;
  }
  this->active_ = false;
  if (outcome != Outcome::Ok && (command_of(query) == QUERY_REMOTE_STATUS || outcome == Outcome::Rejected)) {
    this->wanted_[sid] = 0;
  }
  this->finish_if_done_();
}

void Discovery::on_fault_details(uint8_t sid, uint16_t fault_details) {
  if (this->active_ && this->active_task_.sid == sid && this->active_task_.query == QUERY_FAULT_DETAILS &&
      fault_details != 0) {
    this->wanted_[sid] |= QUERY_FAULT_EVENTS;
  }
}

void Discovery::finish_if_done_() {
  if (this->campaign_ && !this->active_ && this->campaign_pending_ == 0 && !this->has_work()) {
    this->campaign_ = false;
    this->current_sid_ = NO_SID;
    this->finished_ = true;
  }
}

bool Discovery::take_finished() {
  const bool finished = this->finished_;
  this->finished_ = false;
  return finished;
}

void Discovery::clear() {
  for (uint8_t &wanted : this->wanted_) {
    wanted = 0;
  }
  this->waiting_ = false;
  this->want_identify_ = false;
  this->want_details_ = false;
  this->active_ = false;
  this->current_sid_ = NO_SID;
  this->campaign_ = false;
  this->campaign_identify_ = false;
  this->campaign_details_ = false;
  this->campaign_pending_ = 0;
  this->total_ = 0;
  this->index_ = 0;
  this->finished_ = false;
}

void Discovery::set_intervals(uint32_t remote_status_ms, uint32_t battery_ms) {
  this->status_interval_ms_ = remote_status_ms;
  this->battery_interval_ms_ = battery_ms;
  this->periodic_started_ = false;
}

uint8_t Discovery::pick_(const Registry &registry, uint8_t own_sid, bool battery, uint8_t &cursor,
                         int &count) const {
  count = 0;
  uint8_t chosen = NO_SID;
  for (uint8_t step = 1; step <= MAX_SID + 1; ++step) {
    const uint8_t sid = static_cast<uint8_t>((cursor + step) % (MAX_SID + 1));
    const Node *node = registry.find(sid);
    if (node == nullptr || !node->in_network || sid == own_sid || node->is_own()) {
      continue;
    }
    if (battery && (!node->has_battery_report() || node->reach == Reach::Unreachable)) {
      continue;
    }
    count++;
    if (chosen == NO_SID) {
      chosen = sid;
    }
  }
  if (chosen != NO_SID) {
    cursor = chosen;
  }
  return chosen;
}

void Discovery::tick(uint32_t now_ms, const Registry &registry, uint8_t own_sid) {
  if (this->status_interval_ms_ == 0 && this->battery_interval_ms_ == 0) {
    return;
  }
  if (!this->periodic_started_) {
    this->periodic_started_ = true;
    this->next_status_ms_ = now_ms + PERIODIC_START_DELAY_MS;
    this->next_battery_ms_ = now_ms + PERIODIC_START_DELAY_MS;
    return;
  }
  // Während Erkundung oder Netzdetails pausiert die periodische Planung.
  if (this->campaign_ || this->waiting_) {
    return;
  }
  int count = 0;
  if (this->status_interval_ms_ != 0 && due(now_ms, this->next_status_ms_)) {
    const uint8_t sid = this->pick_(registry, own_sid, false, this->status_cursor_, count);
    if (sid != NO_SID) {
      this->want(sid, QUERY_REMOTE_STATUS);
      this->next_status_ms_ = now_ms + this->status_interval_ms_ / static_cast<uint32_t>(count);
    } else {
      this->next_status_ms_ = now_ms + PERIODIC_RETRY_MS;
    }
  }
  if (this->battery_interval_ms_ != 0 && due(now_ms, this->next_battery_ms_)) {
    const uint8_t sid = this->pick_(registry, own_sid, true, this->battery_cursor_, count);
    if (sid != NO_SID) {
      this->want(sid, QUERY_BATTERY);
      this->next_battery_ms_ = now_ms + this->battery_interval_ms_ / static_cast<uint32_t>(count);
    } else {
      this->next_battery_ms_ = now_ms + PERIODIC_RETRY_MS;
    }
  }
}

}  // namespace wisafe2
}  // namespace esphome
