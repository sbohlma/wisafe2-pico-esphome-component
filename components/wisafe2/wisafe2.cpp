#include "wisafe2.h"

#include "catalog.h"
#include "core_ipc.h"
#include "frame_analyzer.h"
#include "node_store.h"
#include "node_view.h"
#include "esphome/components/network/util.h"
#include "esphome/core/hal.h"
#include "esphome/core/log.h"

#include <cmath>
#include <cstdio>
#include <cstring>
#include <ctime>

namespace esphome {
namespace wisafe2 {

static const char *const TAG = "wisafe2";

static constexpr uint8_t INIT_PAYLOAD[] = {0xD3, 0x19, 0x50, 0x00};
static constexpr uint8_t RM_STATUS_PAYLOAD[] = {0xD1};
static constexpr uint8_t RM_STATUS_REPLY[] = {MSG_RM_STATUS};
static constexpr uint8_t SID_MAP_PAYLOAD[] = {0xD3, 0x03};
static constexpr uint8_t SID_MAP_REPLY[] = {MSG_RM_REPORT, 0x03};
static constexpr uint8_t ID_INFO_REPLY[] = {0xC4};
static constexpr uint8_t OWN_CLASS = 0xFF;
// Gibt Log-Clients nach dem WLAN-Aufbau Zeit, sich zu verbinden.
static constexpr uint32_t MODULE_START_DELAY_MS = 5000;
static constexpr int INIT_ATTEMPTS = 50;
// Nach der Quittung des Inits nimmt das Modul keine Bytes an, bis es D4 10 gemeldet hat (etwa 3 s).
static constexpr uint32_t INIT_REPORT_TIMEOUT_MS = 10000;
// nodes.bin: Änderungen sammeln, dann in einer Ruhephase schreiben. Das Schreiben hält Kern 1 an (R5).
static constexpr uint32_t STORE_DELAY_MS = 5000;
static constexpr uint32_t STORE_QUIET_MS = 1000;
// Während einer Erkundung ist fast immer eine Fernanfrage offen; spätestens dann wird trotzdem geschrieben.
static constexpr uint32_t STORE_FORCE_MS = 60000;
// Die YAML-Vorlage geht in Abständen ins Log, damit API-Log-Clients keine Zeilen verlieren.
static constexpr uint32_t TEMPLATE_STEP_MS = 100;

static const char *const STATUS_WAIT_NETWORK = "warte auf WLAN";
static const char *const STATUS_INIT = "Init läuft";
static const char *const STATUS_READY = "Modul bereit";
static const char *const STATUS_NO_REPLY = "keine Antwort";
static const char *const STATUS_UNPAIRED = "ungekoppelt";
static const char *const STATUS_ID_MISMATCH = "Geräte-ID weicht ab";

void WiSafe2Component::set_identity(uint8_t id0, uint8_t id1, uint8_t id2, uint16_t model) {
  this->identity_.valid = true;
  this->identity_.device_id[0] = id0;
  this->identity_.device_id[1] = id1;
  this->identity_.device_id[2] = id2;
  this->identity_.model = model;
}

void WiSafe2Component::publish_status_(const char *status) {
  if (this->status_ == status) {
    return;
  }
  this->status_ = status;
  ESP_LOGI(TAG, "Status: %s", status);
  if (this->status_sensor_ != nullptr) {
    this->status_sensor_->publish_state(status);
  }
}

void WiSafe2Component::update_module_status_() {
  if (!this->paired_) {
    this->publish_status_(STATUS_UNPAIRED);
  } else if (this->id_mismatch_) {
    this->publish_status_(STATUS_ID_MISMATCH);
  } else {
    this->publish_status_(STATUS_READY);
  }
}

void WiSafe2Component::publish_last_reply_(const char *text) {
  if (this->last_reply_sensor_ != nullptr) {
    this->last_reply_sensor_->publish_state(text);
  }
}

bool WiSafe2Component::send_bytes(const uint8_t *bytes, size_t len, uint8_t &id) {
  const uint8_t command_id = ++this->next_command_id_;
  if (!ipc::send_command(command_id, bytes, len)) {
    return false;
  }
  id = command_id;
  char text[ipc::Command::MAX_LEN * 3 + 1];
  size_t used = 0;
  for (size_t index = 0; index < len && used + 4 <= sizeof(text); ++index) {
    used += std::snprintf(text + used, sizeof(text) - used, index == 0 ? "%02X" : " %02X", bytes[index]);
  }
  text[used] = '\0';
  ESP_LOGD(TAG, "Sende %s", text);
  return true;
}

void WiSafe2Component::start_module_() {
  if (!this->module_started_) {
    ipc::request_spi_setup();
    this->module_started_ = true;
  }
  this->start_state_ = StartState::Running;
}

bool WiSafe2Component::enqueue_(const Request &request) {
  if (!this->scheduler_.enqueue(request)) {
    ESP_LOGW(TAG, "Warteschlange voll, Befehl %02X verworfen", request.payload[0]);
    return false;
  }
  return true;
}

void WiSafe2Component::enqueue_init_() {
  this->init_pending_ = true;
  this->await_init_report_ = false;
  this->init_attempt_++;
  this->publish_status_(STATUS_INIT);
  this->enqueue_(Request::remote(TAG_INIT, INIT_PAYLOAD, sizeof(INIT_PAYLOAD)));
}

void WiSafe2Component::start_init() {
  this->start_module_();
  this->init_attempt_ = 0;
  if (!this->init_pending_) {
    this->enqueue_init_();
  }
}

void WiSafe2Component::query_rm_status() {
  this->start_module_();
  this->enqueue_(
      Request::local(TAG_RM_STATUS, RM_STATUS_PAYLOAD, sizeof(RM_STATUS_PAYLOAD), RM_STATUS_REPLY, sizeof(RM_STATUS_REPLY)));
}

void WiSafe2Component::query_sid_map() {
  this->start_module_();
  this->enqueue_(Request::local(TAG_SID_MAP, SID_MAP_PAYLOAD, sizeof(SID_MAP_PAYLOAD), SID_MAP_REPLY, sizeof(SID_MAP_REPLY)));
}

Request WiSafe2Component::remote_status_request_(uint8_t tag, uint8_t sid) const {
  const uint8_t payload[] = {0xD3, 0x06, sid, 0x00};
  const uint8_t reply[] = {MSG_RM_REPORT, 0x06, sid};
  return Request::remote(tag, payload, sizeof(payload), reply, sizeof(reply)).to_sid(sid).about(sid);
}

Request WiSafe2Component::identity_request_(uint8_t tag, uint8_t sid) const {
  const uint8_t payload[] = {0xD3, 0x06, sid, 0x01};
  return Request::remote(tag, payload, sizeof(payload), ID_INFO_REPLY, sizeof(ID_INFO_REPLY))
      .from_sid(sid)
      .to_sid(sid)
      .about(sid);
}

Request WiSafe2Component::device_request_(uint8_t tag, uint8_t request_code, const uint8_t *id, uint16_t model,
                                          uint8_t sid) const {
  uint8_t last = 0x00;
  if (request_code == 0xC7) {
    last = model == MODEL_WHM_1 ? 0x05 : 0x06;
  } else if (request_code == 0xC5) {
    last = 0x74;
  }
  uint8_t payload[7] = {request_code, id[0], id[1], id[2], 0, 0, last};
  write_model(model, payload + 4);
  const uint8_t reply[] = {static_cast<uint8_t>(request_code + 1), id[0], id[1], id[2]};
  return Request::remote(tag, payload, sizeof(payload), reply, sizeof(reply)).about(sid);
}

void WiSafe2Component::query_remote_status(uint8_t sid) {
  if (sid > MAX_SID) {
    ESP_LOGW(TAG, "Remote-Status: SID %u ungültig (0–63)", sid);
    return;
  }
  this->start_module_();
  ESP_LOGI(TAG, "Abfrage Remote-Status SID %u", sid);
  this->enqueue_(this->remote_status_request_(TAG_QUERY_REMOTE_STATUS, sid));
}

void WiSafe2Component::query_identity(uint8_t sid) {
  if (sid > MAX_SID) {
    ESP_LOGW(TAG, "ID-Abfrage: SID %u ungültig (0–63)", sid);
    return;
  }
  this->start_module_();
  ESP_LOGI(TAG, "Abfrage ID-Information SID %u", sid);
  this->enqueue_(this->identity_request_(TAG_QUERY_IDENTITY, sid));
}

void WiSafe2Component::query_device(uint8_t request_code, uint8_t sid, const std::string &device_id, uint16_t model) {
  const uint8_t query = query_for_payload(&request_code, 1);
  if (query == 0) {
    ESP_LOGW(TAG, "Abfrage %02X nicht unterstützt", request_code);
    return;
  }
  const char *what = query_name(query);

  uint8_t id[3];
  bool have_id = false;
  if (!device_id.empty()) {
    if (!parse_device_id(device_id.c_str(), id)) {
      ESP_LOGW(TAG, "%s: device_id \"%s\" ungültig, erwartet 6 Hex-Ziffern wie 2FE43D", what, device_id.c_str());
      return;
    }
    have_id = true;
  }
  const Node *node = have_id ? this->registry_.find_device(id) : this->registry_.find(sid);
  if (have_id && node == nullptr && sid <= MAX_SID) {
    node = this->registry_.find(sid);
  }
  if (!have_id && node != nullptr && node->has_device()) {
    std::memcpy(id, node->device_id, 3);
    have_id = true;
  }
  if (model == 0 && node != nullptr && node->has_model()) {
    model = node->model;
  }
  if (!have_id || model == 0) {
    char text[160];
    std::snprintf(text, sizeof(text), "%s SID %u: %s unbekannt; erst Remote-Status und ID-Information abfragen", what,
                  sid, have_id ? "Modell" : "Geräte-ID");
    ESP_LOGW(TAG, "%s", text);
    this->publish_last_reply_(text);
    return;
  }

  this->start_module_();
  char shown[8];
  format_device_id(id, shown, sizeof(shown));
  ESP_LOGI(TAG, "Abfrage %s Gerät %s, Modell %u", what, shown, model);
  const uint8_t node_sid = node != nullptr ? node->sid : sid;
  this->enqueue_(this->device_request_(TAG_QUERY_DEVICE, request_code, id, model, node_sid));
}

void WiSafe2Component::discover_network() {
  if (!this->paired_) {
    ESP_LOGW(TAG, "Erkundung: RM ungekoppelt");
    return;
  }
  ESP_LOGI(TAG, "Erkundung der SIDs ohne vollständigen Eintrag angefordert");
  this->discovery_.request(Discovery::Scope::Identify);
  this->query_sid_map();
}

void WiSafe2Component::query_network_details() {
  if (!this->paired_) {
    ESP_LOGW(TAG, "Netzdetails: RM ungekoppelt");
    return;
  }
  ESP_LOGI(TAG, "Netzdetails aller SIDs angefordert");
  this->discovery_.request(Discovery::Scope::Details);
  this->query_sid_map();
}

void WiSafe2Component::rediscover_network() {
  ESP_LOGI(TAG, "Register und nodes.bin werden gelöscht, Erkundung aller SIDs folgt");
  this->registry_.clear();
  this->discovery_.clear();
  this->assign_needed_ = true;
  this->store_dirty_ = false;
  if (this->store_ok_ && !node_store_remove()) {
    ESP_LOGW(TAG, "nodes.bin ließ sich nicht löschen");
  }
  if (!this->paired_) {
    ESP_LOGW(TAG, "Erkundung: RM ungekoppelt");
    return;
  }
  this->discovery_.request(Discovery::Scope::Identify);
  this->query_rm_status();
  this->query_sid_map();
}

void WiSafe2Component::log_registry() {
  const uint32_t now = millis();
  int members = 0;
  int complete = 0;
  for (size_t index = 0; index < this->registry_.count(); ++index) {
    const Node &node = this->registry_.at(index);
    members += node.in_network ? 1 : 0;
    complete += node.complete() ? 1 : 0;
  }
  ESP_LOGI(TAG, "Register: %u Knoten, %d im Netz, %d vollständig", static_cast<unsigned>(this->registry_.count()),
           members, complete);
  char text[256];
  for (uint8_t sid = 0; sid <= MAX_SID; ++sid) {
    const Node *node = this->registry_.find(sid);
    if (node != nullptr) {
      describe_node(*node, now, text, sizeof(text));
      ESP_LOGI(TAG, "  %s", text);
    }
  }
}

void WiSafe2Component::query_after_init_() {
  this->await_init_report_ = false;
  if (!this->startup_discovery_requested_) {
    // E5: beim Start nur SIDs ohne Eintrag erkunden.
    this->startup_discovery_requested_ = true;
    this->discovery_.request(Discovery::Scope::Identify);
  }
  this->query_rm_status();
  this->query_sid_map();
}

void WiSafe2Component::refresh_detector(Detector *detector) {
  if (this->assign_needed_) {
    this->assign_detectors_();
  }
  const Node *node = this->registry_.find(detector->sid());
  if (node == nullptr) {
    ESP_LOGW(TAG, "Melder \"%s\" ist keinem Knoten im Register zugeordnet; erst das Netz erkunden", detector->name());
    return;
  }
  if (node->is_own() || !this->paired_) {
    ESP_LOGW(TAG, "Melder \"%s\": %s", detector->name(), node->is_own() ? "eigenes RM der Brücke" : "RM ungekoppelt");
    return;
  }
  ESP_LOGI(TAG, "Status von \"%s\" (SID %u) angefordert", detector->name(), node->sid);
  this->discovery_.want(node->sid, QUERY_DETAILS);
}

void WiSafe2Component::assign_detectors_() {
  this->assign_needed_ = false;
  this->publish_all_ = true;
  uint64_t assigned = 0;
  for (Detector *detector : this->detectors_) {
    const Node *node = detector->assign(this->registry_);
    if (node != nullptr) {
      assigned |= uint64_t{1} << node->sid;
    }
  }
  this->assigned_ = assigned;
}

void WiSafe2Component::publish_entities_(uint32_t now) {
  if (this->assign_needed_) {
    this->assign_detectors_();
  }
  if (this->discovery_running_sensor_ != nullptr) {
    this->discovery_running_sensor_->publish_state(this->discovery_.campaign_running());
  }
  if (!this->publish_all_ && this->dirty_sids_ == 0) {
    return;
  }
  const int64_t epoch = static_cast<int64_t>(::time(nullptr));
  for (Detector *detector : this->detectors_) {
    const uint8_t sid = detector->sid();
    if (this->publish_all_ || (sid <= MAX_SID && ((this->dirty_sids_ >> sid) & 1u) != 0)) {
      detector->publish(this->registry_.find(sid), this->map_seen_, now, epoch);
    }
  }
  const AlarmState alarm = this->registry_.network_alarm();
  publish_tri(this->network_alarm_sensor_, alarm == AlarmState::Active  ? Tri::On
                                           : alarm == AlarmState::Ended ? Tri::Off
                                                                        : Tri::Unknown);
  if (this->network_silenced_sensor_ != nullptr) {
    this->network_silenced_sensor_->publish_state(this->registry_.any_silenced());
  }
  if (this->map_seen_ && this->node_count_sensor_ != nullptr) {
    int members = 0;
    for (size_t index = 0; index < this->registry_.count(); ++index) {
      members += this->registry_.at(index).in_network ? 1 : 0;
    }
    publish_number(this->node_count_sensor_, true, static_cast<float>(members));
  }
  if (this->unassigned_sensor_ != nullptr) {
    char text[200];
    describe_unassigned(this->registry_, this->assigned_, text, sizeof(text));
    publish_text(this->unassigned_sensor_, text);
  }
  this->dirty_sids_ = 0;
  this->publish_all_ = false;
}

void WiSafe2Component::report_event_(const RadioMessage &msg) {
  const char *type = event_type(msg.type);
  if (type == nullptr) {
    return;
  }
  if (this->assign_needed_) {
    this->assign_detectors_();
  }
  const char *name = nullptr;
  for (Detector *detector : this->detectors_) {
    if (detector->sid() == msg.sid) {
      detector->trigger(type);
      if (name == nullptr) {
        name = detector->name();
      }
    }
  }
  if (this->network_event_ != nullptr) {
    this->network_event_->trigger(type);
  }
  char text[160];
  describe_event(msg, name, text, sizeof(text));
  ESP_LOGI(TAG, "Ereignis: %s", text);
  if (this->last_event_sensor_ != nullptr) {
    this->last_event_sensor_->publish_state(text);
  }
}

bool WiSafe2Component::next_template_node_(const Node *&node) {
  for (; this->template_sid_ <= MAX_SID; ++this->template_sid_) {
    node = this->registry_.find(this->template_sid_);
    if (node != nullptr && node_assignable(*node) && ((this->assigned_ >> this->template_sid_) & 1u) == 0) {
      ++this->template_sid_;
      return true;
    }
  }
  return false;
}

void WiSafe2Component::log_yaml_template() {
  if (this->assign_needed_) {
    this->assign_detectors_();
  }
  int count = 0;
  for (uint8_t sid = 0; sid <= MAX_SID; ++sid) {
    const Node *node = this->registry_.find(sid);
    count += node != nullptr && node_assignable(*node) && ((this->assigned_ >> sid) & 1u) == 0 ? 1 : 0;
  }
  if (count == 0) {
    ESP_LOGI(TAG, "YAML-Vorlage: Alle Melder im Register stehen in detectors");
    return;
  }
  // Kopfzeilen als YAML-Kommentar: Ohne Zeitstempel und Log-Präfix bleibt die Vorlage gültiges YAML.
  ESP_LOGI(TAG, "# YAML-Vorlage für %d Melder ohne Eintrag in detectors, Namen nach Wunsch anpassen\n"
                "esphome:\n"
                "  devices:",
           count);
  this->template_phase_ = TemplatePhase::Devices;
  this->template_sid_ = 0;
  this->template_last_ms_ = millis();
}

void WiSafe2Component::service_template_(uint32_t now) {
  if (this->template_phase_ == TemplatePhase::Idle || now - this->template_last_ms_ < TEMPLATE_STEP_MS) {
    return;
  }
  this->template_last_ms_ = now;
  const Node *node = nullptr;
  if (!this->next_template_node_(node)) {
    if (this->template_phase_ == TemplatePhase::Devices) {
      ESP_LOGI(TAG, "# YAML-Vorlage, Fortsetzung\n"
                    "wisafe2:\n"
                    "  detectors:");
      this->template_phase_ = TemplatePhase::Detectors;
      this->template_sid_ = 0;
    } else {
      ESP_LOGI(TAG, "# YAML-Vorlage Ende");
      this->template_phase_ = TemplatePhase::Idle;
    }
    return;
  }
  const bool by_device = template_by_device(this->registry_, *node);
  char name[40];
  char key[24];
  template_name(*node, by_device, name, sizeof(name));
  template_device_key(*node, by_device, key, sizeof(key));
  if (this->template_phase_ == TemplatePhase::Devices) {
    ESP_LOGI(TAG, "    # SID %u\n"
                  "    - id: %s\n"
                  "      name: \"%s\"",
             node->sid, key, name);
    return;
  }
  char id_line[40] = "";
  if (by_device) {
    char id[8];
    format_device_id(node->device_id, id, sizeof(id));
    std::snprintf(id_line, sizeof(id_line), "\n      device_id: \"%s\"", id);
  }
  const char *device_class = node->has_class() ? alarm_device_class(node->device_class) : nullptr;
  char alarm_lines[64] = "";
  if (device_class != nullptr && std::strcmp(device_class, "smoke") != 0) {
    std::snprintf(alarm_lines, sizeof(alarm_lines), "\n      alarm:\n        device_class: %s", device_class);
  }
  ESP_LOGI(TAG, "    # SID %u\n"
                "    - sid: %u%s\n"
                "      device: %s%s",
           node->sid, node->sid, id_line, key, alarm_lines);
}

void WiSafe2Component::note_changes_(uint8_t sid, uint8_t changes) {
  if ((changes & (CHANGE_NEW | CHANGE_IDENTITY | CHANGE_DEVICE)) != 0) {
    this->assign_needed_ = true;
  }
  if (changes != 0) {
    if (sid <= MAX_SID) {
      this->dirty_sids_ |= uint64_t{1} << sid;
    } else {
      this->publish_all_ = true;
    }
  }
  if ((changes & CHANGE_IDENTITY) != 0) {
    this->mark_dirty_();
  }
  const Node *node = this->registry_.find(sid);
  if (node == nullptr) {
    return;
  }
  char text[256] = "";
  if ((changes & (CHANGE_IDENTITY | CHANGE_REACH)) != 0) {
    describe_node(*node, millis(), text, sizeof(text));
  }
  if ((changes & CHANGE_DEVICE) != 0) {
    ESP_LOGW(TAG, "Register: neue Geräte-ID für SID %u, Modell und Klasse werden neu ermittelt: %s", sid, text);
  } else if ((changes & CHANGE_IDENTITY) != 0) {
    ESP_LOGI(TAG, "Register: %s", text);
  }
  if ((changes & CHANGE_REACH) != 0) {
    if (node->reach == Reach::Unreachable) {
      ESP_LOGW(TAG, "SID %u nicht erreichbar", sid);
    } else {
      ESP_LOGI(TAG, "SID %u wieder erreichbar", sid);
    }
  }
}

void WiSafe2Component::want_unknown_(uint64_t map) {
  for (uint8_t sid = 0; sid <= MAX_SID; ++sid) {
    if (((map >> sid) & 1u) != 0 && sid != this->own_sid_ && !this->registry_.known(sid)) {
      ESP_LOGI(TAG, "Unbekannte SID %u gemeldet, wird erkundet", sid);
      this->discovery_.want(sid, QUERY_IDENTIFY);
    }
  }
}

void WiSafe2Component::start_discovery_(uint64_t map) {
  const bool running = this->discovery_.campaign_running();
  const int targets = this->discovery_.on_map(map, this->registry_, this->own_sid_);
  if (targets == 0) {
    if (!running) {
      ESP_LOGI(TAG, "Register vollständig, keine Erkundung nötig");
      this->log_registry();
    }
    return;
  }
  if (!running) {
    this->discovery_started_ms_ = millis();
  }
  ESP_LOGI(TAG, "%s von %d SID(s) beginnt, Zeitlimit je Antwort %u s", this->discovery_.campaign_name(), targets,
           static_cast<unsigned>(this->scheduler_.remote_timeout() / 1000u));
}

bool WiSafe2Component::discovery_ready_() const {
  return this->paired_ && !this->init_pending_ && !this->await_init_report_ && !this->discovery_.busy() &&
         !this->scheduler_.busy() && !this->scheduler_.remote_open() && this->scheduler_.pending() == 0;
}

void WiSafe2Component::enqueue_discovery_(const DiscoveryTask &task) {
  const Node *node = this->registry_.find(task.sid);
  Request request{};
  switch (task.query) {
    case QUERY_LOCATE:
    case QUERY_REMOTE_STATUS:
      request = this->remote_status_request_(TAG_DISCOVERY, task.sid);
      break;
    case QUERY_IDENTITY:
      request = this->identity_request_(TAG_DISCOVERY, task.sid);
      break;
    case QUERY_BATTERY:
    case QUERY_FAULT_DETAILS:
    case QUERY_FAULT_EVENTS: {
      const uint8_t code = task.query == QUERY_BATTERY ? 0xC7 : task.query == QUERY_FAULT_DETAILS ? 0x72 : 0xC5;
      request = this->device_request_(TAG_DISCOVERY, code, node->device_id, node->model, task.sid);
      break;
    }
    default:
      this->discovery_.on_result(task.sid, task.query, Outcome::SendFailed);
      return;
  }
  if (this->discovery_.campaign_running()) {
    ESP_LOGI(TAG, "%s SID %u (%d/%d): %s", this->discovery_.campaign_name(), task.sid,
             this->discovery_.campaign_index(), this->discovery_.campaign_total(), query_name(task.query));
  } else {
    ESP_LOGI(TAG, "Abfrage SID %u: %s", task.sid, query_name(task.query));
  }
  if (!this->enqueue_(request)) {
    this->discovery_.on_result(task.sid, task.query, Outcome::SendFailed);
  }
}

void WiSafe2Component::service_discovery_(uint32_t now) {
  if (this->auto_discovery_) {
    this->discovery_.tick(now, this->registry_, this->own_sid_);
  }
  if (this->discovery_.has_work() && this->discovery_ready_()) {
    DiscoveryTask task;
    if (this->discovery_.next(this->registry_, task)) {
      this->enqueue_discovery_(task);
    }
  }
  if (this->discovery_.take_finished()) {
    ESP_LOGI(TAG, "%s abgeschlossen nach %u s", this->discovery_.campaign_name(),
             static_cast<unsigned>((now - this->discovery_started_ms_) / 1000u));
    this->log_registry();
    this->log_yaml_template();
  }
}

void WiSafe2Component::load_registry_() {
  if (!node_store_begin()) {
    this->store_state_ = "Dateisystem nicht verfügbar, Register nur im RAM";
    ESP_LOGW(TAG, "%s", this->store_state_);
    return;
  }
  this->store_ok_ = true;
  uint8_t data[REGISTRY_FILE_LEN];
  size_t read = 0;
  switch (node_store_read(data, sizeof(data), read)) {
    case StoreRead::Missing:
      this->store_state_ = "nodes.bin fehlt, Register startet leer";
      ESP_LOGI(TAG, "%s", this->store_state_);
      return;
    case StoreRead::Error:
      this->store_state_ = "nodes.bin nicht lesbar, Register startet leer";
      ESP_LOGW(TAG, "%s", this->store_state_);
      return;
    case StoreRead::Ok:
      break;
  }
  const LoadResult result = this->registry_.deserialize(data, read);
  if (result != LoadResult::Ok) {
    this->store_state_ = "nodes.bin verworfen, Register startet leer";
    ESP_LOGW(TAG, "nodes.bin verworfen (%s), Register startet leer", load_result_name(result));
    return;
  }
  this->store_state_ = "aus nodes.bin geladen";
  ESP_LOGI(TAG, "Register aus nodes.bin geladen: %u Knoten", static_cast<unsigned>(this->registry_.count()));
}

void WiSafe2Component::mark_dirty_() {
  if (!this->store_dirty_) {
    this->store_dirty_ = true;
    this->store_dirty_since_ms_ = millis();
  }
}

void WiSafe2Component::service_store_(uint32_t now) {
  if (!this->store_dirty_ || !this->store_ok_) {
    return;
  }
  const uint32_t age = now - this->store_dirty_since_ms_;
  if (age < STORE_DELAY_MS || this->decoder_.open() || this->scheduler_.busy() ||
      now - this->last_frame_ms_ < STORE_QUIET_MS) {
    return;
  }
  if (this->scheduler_.remote_open() && age < STORE_FORCE_MS) {
    return;
  }
  uint8_t data[REGISTRY_FILE_LEN];
  const size_t len = this->registry_.serialize(data, sizeof(data));
  const uint32_t start = millis();
  if (len == 0 || !node_store_write(data, len)) {
    ESP_LOGW(TAG, "nodes.bin ließ sich nicht schreiben, neuer Versuch später");
    this->store_dirty_since_ms_ = now;
    return;
  }
  this->store_dirty_ = false;
  ESP_LOGI(TAG, "nodes.bin gespeichert: %u Plätze belegt, %u ms", data[5], static_cast<unsigned>(millis() - start));
}

void WiSafe2Component::report_query_(const Completion &completion) {
  const Request &request = completion.request;
  char text[256];
  if (completion.outcome == Outcome::Ok && completion.reply != nullptr) {
    describe_frame(*completion.reply, text, sizeof(text), false);
    this->publish_last_reply_(text);
    return;
  }
  char label[48];
  if (request.tag == TAG_QUERY_DEVICE) {
    char shown[8];
    format_device_id(request.payload + 1, shown, sizeof(shown));
    std::snprintf(label, sizeof(label), "%s Gerät %s", query_name(query_for_payload(request.payload, request.len)),
                  shown);
  } else {
    std::snprintf(label, sizeof(label), "%s SID %u",
                  request.tag == TAG_QUERY_REMOTE_STATUS ? "Remote-Status" : "ID-Information", request.payload[2]);
  }
  std::snprintf(text, sizeof(text), "%s: %s", label, outcome_name(completion.outcome));
  ESP_LOGW(TAG, "%s", text);
  this->publish_last_reply_(text);
}

void WiSafe2Component::handle_completion_(const Completion &completion) {
  const Request &request = completion.request;
  switch (request.tag) {
    case TAG_INIT:
      this->init_pending_ = false;
      if (completion.outcome == Outcome::Ok) {
        ESP_LOGI(TAG, "Init quittiert nach %d Versuch(en), warte auf D4 10", this->init_attempt_);
        this->update_module_status_();
        this->await_init_report_ = true;
        this->init_report_deadline_ms_ = millis() + INIT_REPORT_TIMEOUT_MS;
      } else if (this->init_attempt_ < INIT_ATTEMPTS) {
        ESP_LOGD(TAG, "Init %s, Versuch %d/%d", outcome_name(completion.outcome), this->init_attempt_,
                 INIT_ATTEMPTS);
        this->enqueue_init_();
      } else {
        ESP_LOGW(TAG, "Init nach %d Versuchen ohne Quittung", INIT_ATTEMPTS);
        this->publish_status_(STATUS_NO_REPLY);
      }
      break;
    case TAG_RM_STATUS:
    case TAG_SID_MAP:
      if (completion.outcome != Outcome::Ok) {
        ESP_LOGW(TAG, "%s: %s nach %u Sendeversuch(en)", request.tag == TAG_RM_STATUS ? "D1 (RM-Status)" : "D3 03 (SID-Map)",
                 outcome_name(completion.outcome), static_cast<unsigned>(request.send_attempts));
      }
      break;
    case TAG_IDENTITY_REPLY:
    case TAG_HOST_REPLY:
      if (completion.outcome != Outcome::Ok) {
        ESP_LOGW(TAG, "Antwort %02X: %s", request.payload[0], outcome_name(completion.outcome));
      }
      break;
    case TAG_QUERY_REMOTE_STATUS:
    case TAG_QUERY_IDENTITY:
    case TAG_QUERY_DEVICE:
    case TAG_DISCOVERY: {
      const uint8_t query = query_for_payload(request.payload, request.len);
      if (request.node_sid != NO_SID && query != 0) {
        this->note_changes_(request.node_sid,
                            this->registry_.on_query_result(request.node_sid, query, completion.outcome));
      }
      if (request.tag != TAG_DISCOVERY) {
        this->report_query_(completion);
        break;
      }
      if (completion.outcome != Outcome::Ok) {
        ESP_LOGI(TAG, "SID %u: %s %s", request.node_sid, query_name(query), outcome_name(completion.outcome));
      }
      this->discovery_.on_result(request.node_sid, query, completion.outcome);
      break;
    }
    default:
      break;
  }
}

void WiSafe2Component::handle_rm_status_(const LocalMessage &msg) {
  const RmState &rm = msg.rm;
  this->paired_ = msg.sid != SID_UNPAIRED;
  char id_text[8];
  format_device_id(rm.device_id, id_text, sizeof(id_text));
  char fw_text[4];
  std::snprintf(fw_text, sizeof(fw_text), "%02X", rm.firmware);

  if (this->rm_sid_sensor_ != nullptr) {
    this->rm_sid_sensor_->publish_state(this->paired_ ? static_cast<float>(msg.sid) : NAN);
  }
  if (this->rm_battery_sensor_ != nullptr) {
    this->rm_battery_sensor_->publish_state(static_cast<float>(rm_battery_mv(rm.bvnl)) / 1000.0f);
  }
  if (this->rm_rssi_sensor_ != nullptr) {
    this->rm_rssi_sensor_->publish_state(rm.rssi);
  }
  if (this->rm_radio_faults_sensor_ != nullptr) {
    this->rm_radio_faults_sensor_->publish_state(rm.radio_faults);
  }
  if (this->rm_firmware_sensor_ != nullptr) {
    this->rm_firmware_sensor_->publish_state(fw_text);
  }
  if (this->rm_device_id_sensor_ != nullptr) {
    this->rm_device_id_sensor_->publish_state(id_text);
  }
  if (this->rm_paired_sensor_ != nullptr) {
    this->rm_paired_sensor_->publish_state(this->paired_);
  }

  if (this->identity_.valid && this->paired_) {
    const bool mismatch = std::memcmp(rm.device_id, this->identity_.device_id, 3) != 0;
    if (mismatch) {
      char own[8];
      format_device_id(this->identity_.device_id, own, sizeof(own));
      ESP_LOGW(TAG, "Geräte-ID im Modul %s weicht von device_id %s ab", id_text, own);
    } else if (!this->id_confirmed_) {
      ESP_LOGI(TAG, "Geräte-ID im Modul stimmt mit device_id %s überein", id_text);
    }
    this->id_mismatch_ = mismatch;
    this->id_confirmed_ = !mismatch;
  }
  this->update_module_status_();
}

void WiSafe2Component::handle_local_(const LocalMessage &msg, uint32_t now) {
  switch (msg.type) {
    case LocalType::IdentityRequest: {
      uint8_t reply[HOST_REPLY_MAX];
      const size_t len = build_identity_reply(this->identity_, reply, sizeof(reply));
      if (len == 0) {
        ESP_LOGW(TAG, "41 bleibt unbeantwortet: keine device_id in der YAML");
        return;
      }
      ESP_LOGI(TAG, "Beantworte 41 mit 91");
      this->enqueue_(Request::host_reply(TAG_IDENTITY_REPLY, reply, len, false));
      break;
    }
    case LocalType::RmStatus: {
      // D2 kommt auch unaufgefordert etwa stündlich.
      this->handle_rm_status_(msg);
      if (!this->paired_) {
        break;
      }
      this->own_sid_ = msg.sid;
      uint8_t changes = this->registry_.apply(msg, now);
      if (this->identity_.valid) {
        changes |= this->registry_.set_own(msg.sid, this->identity_.device_id, this->identity_.model, OWN_CLASS);
      }
      this->note_changes_(msg.sid, changes);
      break;
    }
    case LocalType::LocalSidMap:
      this->paired_ = msg.map != 0;
      if (this->rm_paired_sensor_ != nullptr) {
        this->rm_paired_sensor_->publish_state(this->paired_);
      }
      this->update_module_status_();
      this->map_seen_ = true;
      this->note_changes_(NO_SID, this->registry_.apply(msg, now) | CHANGE_STATE);
      if (this->paired_ && this->discovery_.waiting_for_map()) {
        this->start_discovery_(msg.map);
      }
      break;
    case LocalType::NewNetworkMap:
      this->map_seen_ = true;
      this->note_changes_(NO_SID, this->registry_.apply(msg, now) | CHANGE_STATE);
      if (this->auto_discovery_) {
        this->want_unknown_(msg.map);
      }
      break;
    case LocalType::PresenceHeard:
      this->note_changes_(msg.sid, this->registry_.apply(msg, now));
      if (this->auto_discovery_ && msg.sid <= MAX_SID) {
        this->want_unknown_(uint64_t{1} << msg.sid);
      }
      break;
    case LocalType::RemoteStatus:
      this->note_changes_(msg.sid, this->registry_.apply(msg, now));
      break;
    case LocalType::MissingMap: {
      uint64_t was_unreachable = 0;
      for (size_t index = 0; index < this->registry_.count(); ++index) {
        const Node &node = this->registry_.at(index);
        if (node.reach == Reach::Unreachable) {
          was_unreachable |= uint64_t{1} << node.sid;
        }
      }
      this->registry_.apply(msg, now);
      const uint64_t newly = msg.map & ~was_unreachable;
      for (uint8_t sid = 0; sid <= MAX_SID; ++sid) {
        const Node *node = this->registry_.find(sid);
        if (((newly >> sid) & 1u) != 0 && node != nullptr && node->reach == Reach::Unreachable) {
          this->note_changes_(sid, CHANGE_REACH);
        }
      }
      break;
    }
    case LocalType::InitReport:
      if (msg.sid <= MAX_SID) {
        this->own_sid_ = msg.sid;
      }
      break;
    default:
      break;
  }
}

void WiSafe2Component::handle_radio_(const RadioMessage &msg, uint32_t now) {
  this->note_changes_(msg.sid, this->registry_.apply(msg, now));
  this->report_event_(msg);
  if (msg.type == RadioType::FaultDetails) {
    this->discovery_.on_fault_details(msg.sid, msg.value16);
  }
  if (this->auto_discovery_ && msg.type == RadioType::Fault && (msg.value & 0x02) != 0) {
    // Ist 0x02 gesetzt, stehen die Einzelheiten in 73; meldet 73 Fehler, folgt C6.
    this->discovery_.want(msg.sid, QUERY_FAULT_DETAILS);
  }
  if (!is_host_request(msg.type) || !this->identity_.valid) {
    return;
  }
  uint8_t reply[HOST_REPLY_MAX];
  const size_t len = build_host_reply(msg, this->identity_, reply, sizeof(reply));
  if (len == 0) {
    return;
  }
  if (!this->reply_limiter_.allow(msg.code, now)) {
    ESP_LOGD(TAG, "%02X von SID %u: innerhalb von 60 s schon beantwortet", msg.code, msg.sid);
    return;
  }
  ESP_LOGI(TAG, "Beantworte %02X von SID %u mit %02X", msg.code, msg.sid, reply[0]);
  this->enqueue_(Request::host_reply(TAG_HOST_REPLY, reply, len, true));
}

void WiSafe2Component::handle_frame_(const Frame &frame, uint32_t now) {
  this->last_frame_ms_ = now;
  RadioMessage radio;
  const bool is_radio = parse_radio(frame, radio);
  const bool duplicate = is_radio && this->duplicates_.seen(radio.sid, radio.code, radio.seq, now);

  char text[256];
  describe_frame(frame, text, sizeof(text));
  if (duplicate) {
    ESP_LOGD(TAG, "Duplikat: %s", text);
  } else {
    ESP_LOGI(TAG, "%s", text);
  }

  LocalMessage local;
  if (parse_local(frame, local)) {
    this->handle_local_(local, now);
  } else if (is_radio && !duplicate) {
    this->handle_radio_(radio, now);
  }

  this->scheduler_.on_frame(frame, now);

  if (this->await_init_report_ && local.type == LocalType::InitReport) {
    this->query_after_init_();
  }
}

void WiSafe2Component::setup() {
  this->scheduler_.set_handler([this](const Completion &completion) { this->handle_completion_(completion); });
  if (this->auto_discovery_) {
    this->discovery_.set_intervals(this->remote_status_interval_ms_, this->battery_interval_ms_);
  }
  this->load_registry_();
  ESP_LOGI(TAG, "Modulstart nach WLAN-Verbindung");
  this->publish_status_(STATUS_WAIT_NETWORK);
}

void WiSafe2Component::loop() {
  char text[ipc::LogMessage::TEXT_LEN];
  while (ipc::poll_core1_log(text, sizeof(text))) {
    ESP_LOGI(TAG, "%s", text);
  }
  const uint32_t dropped = ipc::take_dropped_logs();
  if (dropped != 0) {
    ESP_LOGW(TAG, "Kern 1: %u Logzeilen verworfen", dropped);
  }

  uint32_t now = millis();
  ipc::CommandResult result{};
  while (ipc::poll_command_result(result)) {
    if (!result.ok && result.sent == 0) {
      ESP_LOGD(TAG, "Kern 1: Modul nimmt kein Byte an (%s)", ipc::send_error_name(result.error));
    } else if (!result.ok) {
      ESP_LOGW(TAG, "Kern 1: Befehl nach %u Bytes abgebrochen (%s)", static_cast<unsigned>(result.sent),
               ipc::send_error_name(result.error));
    }
    this->scheduler_.on_send_result(result.id, result.ok, result.sent, now);
  }

  Frame frame;
  uint8_t byte = 0;
  while (ipc::poll_spi_byte(byte)) {
    now = millis();
    if (this->decoder_.feed(byte, now, frame)) {
      this->handle_frame_(frame, now);
    }
  }
  now = millis();
  if (this->decoder_.poll_timeout(now, frame)) {
    this->handle_frame_(frame, now);
  }

  switch (this->start_state_) {
    case StartState::WaitNetwork:
      if (network::is_connected()) {
        this->start_state_ = StartState::Settle;
        this->settle_since_ms_ = now;
      }
      break;
    case StartState::Settle:
      if (now - this->settle_since_ms_ >= MODULE_START_DELAY_MS) {
        this->start_init();
      }
      break;
    case StartState::Running:
      if (this->await_init_report_ && static_cast<int32_t>(now - this->init_report_deadline_ms_) >= 0) {
        ESP_LOGW(TAG, "Kein D4 10 nach dem Init, frage trotzdem ab");
        this->query_after_init_();
      }
      this->scheduler_.service(now, this->decoder_.open());
      this->service_discovery_(now);
      this->service_store_(now);
      break;
  }
  this->publish_entities_(now);
  this->service_template_(now);
}

void WiSafe2Component::dump_config() {
  ESP_LOGCONFIG(TAG, "WiSafe2:");
  if (this->identity_.valid) {
    char id_text[8];
    format_device_id(this->identity_.device_id, id_text, sizeof(id_text));
    const char *name = model_name(this->identity_.model);
    ESP_LOGCONFIG(TAG, "  Identität: Geräte-ID %s, Modell %u %s, Klasse FF", id_text, this->identity_.model,
                  name != nullptr ? name : "");
  } else {
    ESP_LOGCONFIG(TAG, "  Identität: keine device_id, 41 und Anfragen bleiben unbeantwortet");
  }
  ESP_LOGCONFIG(TAG, "  Register: %u Knoten, %s", static_cast<unsigned>(this->registry_.count()), this->store_state_);
  if (this->auto_discovery_) {
    ESP_LOGCONFIG(TAG, "  auto_discovery: an, Remote-Status je SID alle %u min, Batterie je Melder alle %u min",
                  static_cast<unsigned>(this->remote_status_interval_ms_ / 60000u),
                  static_cast<unsigned>(this->battery_interval_ms_ / 60000u));
  } else {
    ESP_LOGCONFIG(TAG, "  auto_discovery: aus, Erkundung beim Start für SIDs ohne Eintrag und per Button");
  }
  ESP_LOGCONFIG(TAG, "  Zeitlimit für Fernantworten: %u s",
                static_cast<unsigned>(this->scheduler_.remote_timeout() / 1000u));
  LOG_TEXT_SENSOR("  ", "Status", this->status_sensor_);
  LOG_TEXT_SENSOR("  ", "Letzte Antwort", this->last_reply_sensor_);
  LOG_SENSOR("  ", "RM-SID", this->rm_sid_sensor_);
  LOG_SENSOR("  ", "RM-Batterie", this->rm_battery_sensor_);
  LOG_SENSOR("  ", "RM-RSSI", this->rm_rssi_sensor_);
  LOG_SENSOR("  ", "RM-Funkfehler", this->rm_radio_faults_sensor_);
  LOG_TEXT_SENSOR("  ", "RM-Firmware", this->rm_firmware_sensor_);
  LOG_TEXT_SENSOR("  ", "RM-Geräte-ID", this->rm_device_id_sensor_);
  LOG_BINARY_SENSOR("  ", "RM gekoppelt", this->rm_paired_sensor_);
  LOG_BINARY_SENSOR("  ", "Alarm im Netz", this->network_alarm_sensor_);
  LOG_BINARY_SENSOR("  ", "Stummschaltung im Netz", this->network_silenced_sensor_);
  LOG_BINARY_SENSOR("  ", "Erkundung läuft", this->discovery_running_sensor_);
  LOG_SENSOR("  ", "Anzahl Knoten", this->node_count_sensor_);
  LOG_TEXT_SENSOR("  ", "Letztes Ereignis", this->last_event_sensor_);
  LOG_TEXT_SENSOR("  ", "Nicht zugeordnete Melder", this->unassigned_sensor_);
  LOG_EVENT("  ", "Ereignis im Netz", this->network_event_);
  ESP_LOGCONFIG(TAG, "  Melder in der YAML: %u", static_cast<unsigned>(this->detectors_.size()));
  for (const Detector *detector : this->detectors_) {
    detector->dump_config("    ");
  }
  ESP_LOGCONFIG(TAG, "  SPI-Slave Kern 1: GP16 MOSI, GP17 CS, GP18 SCK, GP19 MISO, GP20 IRQ");
  ESP_LOGCONFIG(TAG, "  Rahmen maskiert, 500 ms Ruhe zwischen Befehlen, Init D3 19 50 00");
}

}  // namespace wisafe2
}  // namespace esphome
