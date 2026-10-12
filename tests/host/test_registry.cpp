#include "check.h"

#include "catalog.h"
#include "messages.h"
#include "registry.h"

#include <initializer_list>
#include <string>

using namespace esphome::wisafe2;

namespace {

Frame make_frame(std::initializer_list<uint8_t> bytes) {
  Frame frame;
  for (uint8_t byte : bytes) {
    frame.data[frame.len++] = byte;
  }
  return frame;
}

uint8_t apply(Registry &registry, std::initializer_list<uint8_t> bytes, uint32_t now = 1000) {
  const Frame frame = make_frame(bytes);
  LocalMessage local;
  if (parse_local(frame, local)) {
    return registry.apply(local, now);
  }
  RadioMessage radio;
  CHECK(parse_radio(frame, radio));
  return registry.apply(radio, now);
}

std::string describe(const Node &node, uint32_t now) {
  char text[256];
  describe_node(node, now, text, sizeof(text));
  return text;
}

// Mitschnitte vom 12.10.2026: Handabfragen an SID 14 (ST-630-DE 2FE43D).
void fill_sid14(Registry &registry) {
  apply(registry, {0xC4, 0x3D, 0xE4, 0x2F, 0x81, 0x7C, 0x04, 0x0E, 0x05});
  apply(registry, {0xD4, 0x06, 0x0E, 0x64, 0x8C, 0x8C, 0x7D, 0xEF, 0x3D, 0xE4, 0x2F, 0x00, 0x00});
  apply(registry, {0xC8, 0x3D, 0xE4, 0x2F, 0x06, 0x8A, 0x0B, 0x00, 0x00, 0x0E, 0x08});
  apply(registry, {0x73, 0x3D, 0xE4, 0x2F, 0x7C, 0x04, 0x00, 0x00, 0x0E, 0x09});
}

void test_crc() {
  const uint8_t text[] = {'1', '2', '3', '4', '5', '6', '7', '8', '9'};
  CHECK_EQ(crc16_ccitt(text, sizeof(text)), 0x29B1);
}

void test_detector_entry() {
  Registry registry;
  const uint8_t changes = apply(registry, {0xC4, 0x3D, 0xE4, 0x2F, 0x81, 0x7C, 0x04, 0x0E, 0x05});
  CHECK((changes & CHANGE_NEW) != 0);
  CHECK((changes & CHANGE_IDENTITY) != 0);
  fill_sid14(registry);
  const Node *node = registry.find(14);
  CHECK(node != nullptr);
  CHECK_EQ(node->model, MODEL_ST_630_DE);
  CHECK_EQ(node->device_class, 0x81);
  CHECK(node->complete());
  CHECK(node->is_detector());
  CHECK(node->has_fault_events_support());
  CHECK(registry.known(14));
  CHECK_EQ(node->battery_mv, 2954);
  CHECK(node->has_fault_details);
  CHECK_EQ(node->fault_details, 0);
  CHECK(node->has_rm);
  CHECK_EQ(rm_battery_mv(node->rm_bvnl), 2723);
  CHECK_EQ(node->rm_rssi, 125);
  CHECK(node->reach == Reach::Reachable);

  // Dieselben Werte ändern die Identität nicht.
  CHECK((apply(registry, {0xC4, 0x3D, 0xE4, 0x2F, 0x81, 0x7C, 0x04, 0x0E, 0x06}) & CHANGE_IDENTITY) == 0);

  apply(registry, {0x71, 0x3D, 0xE4, 0x2F, 0x7C, 0x04, 0x01, 0x0E, 0x0A});
  CHECK_EQ(registry.find(14)->fault_flags, 0x01);
  apply(registry, {0x71, 0x3D, 0xE4, 0x2F, 0x7C, 0x04, 0x05, 0x0E, 0x0B}, 31000);
  CHECK_EQ(registry.find(14)->fault_flags, 0x05);
  apply(registry, {0x70, 0x3D, 0xE4, 0x2F, 0x81, 0x01, 0x7C, 0x04, 0x0E, 0x03}, 31000);
  CHECK(registry.find(14)->has_test);

  const std::string text = describe(*registry.find(14), 43000);
  CHECK_STR_CONTAINS(text.c_str(), "SID 14: 2FE43D ST-630-DE, Rauch");
  CHECK_STR_CONTAINS(text.c_str(), "Batterie 2954 mV");
  CHECK_STR_CONTAINS(text.c_str(), "RM 2723 mV, RSSI 125");
  CHECK_STR_CONTAINS(text.c_str(), "Flags 05 auf Sockel");
  CHECK_STR_CONTAINS(text.c_str(), "FS 0000");
  CHECK_STR_CONTAINS(text.c_str(), "Test ok");
  CHECK_STR_CONTAINS(text.c_str(), "zuletzt vor 12 s");
}

void test_requests_create_no_slot() {
  Registry registry;
  // Anfragen des W2-MM (SID 15) mit zusätzlichem 7E tragen das Zielgerät, hier die Brücke.
  CHECK_EQ(apply(registry, {0xC7, 0xA5, 0xB8, 0x14, 0x11, 0x03, 0x06, 0x7E, 0x0F, 0x0D}), 0);
  CHECK_EQ(apply(registry, {0xC3, 0x00, 0x00, 0x00, 0x0F, 0x0B}), 0);
  CHECK_EQ(registry.count(), 0u);
  const uint8_t bridge[] = {0xA5, 0xB8, 0x14};
  CHECK(registry.find_device(bridge) == nullptr);
}

void test_membership_and_own() {
  Registry registry;
  // 22 Knoten, darunter die eigene SID 20 und SID 16 ohne Batterie.
  const uint8_t changes = apply(registry, {0xD4, 0x03, 0xFF, 0x7F, 0x1F, 0x05, 0x00, 0x00, 0x00, 0x00});
  CHECK((changes & CHANGE_NEW) != 0);
  CHECK((changes & CHANGE_IDENTITY) == 0);
  CHECK_EQ(registry.count(), 22u);
  CHECK_EQ(registry.members(), 0x00000000051F7FFFull);
  CHECK(registry.find(16)->in_network);
  CHECK(!registry.known(16));
  CHECK(registry.find(15) == nullptr);

  apply(registry, {0xD2, 0x3D, 0x4F, 0x4F, 0x60, 0xEF, 0xA5, 0xB8, 0x14, 0x00, 0x00, 0x14, 0x00});
  const uint8_t own_id[] = {0xA5, 0xB8, 0x14};
  registry.set_own(20, own_id, MODEL_WST_630, 0xFF);
  const Node *own = registry.find(20);
  CHECK(own->is_own());
  CHECK(own->complete());
  CHECK(!own->is_detector());
  CHECK_EQ(own->device_class, 0xFF);
  CHECK(registry.find_device(own_id) == own);

  // Ein späteres D2 mit anderer ID überschreibt die Identität aus der YAML nicht.
  CHECK((apply(registry, {0xD2, 0x3D, 0x4F, 0x4F, 0x60, 0xEF, 0x3D, 0xE4, 0x2F, 0x00, 0x00, 0x14, 0x00}) &
         CHANGE_DEVICE) == 0);
  CHECK_EQ(registry.find(20)->device_id[2], 0x14);

  // D4 04 mit kleinerer Karte nimmt Knoten aus dem Netz, behält sie aber im Register.
  apply(registry, {0xD4, 0x04, 0xFF, 0x7F, 0x1E, 0x05, 0x00, 0x00, 0x00, 0x00});
  CHECK(!registry.find(16)->in_network);
  CHECK_EQ(registry.count(), 22u);

  // Ungekoppeltes RM (SID 0x40) legt keinen Platz an.
  Registry empty;
  apply(empty, {0xD2, 0x2D, 0x32, 0x41, 0x00, 0xDF, 0x00, 0x00, 0x00, 0x00, 0x01, 0x40, 0x40});
  CHECK_EQ(empty.count(), 0u);
}

void test_reachability() {
  Registry registry;
  fill_sid14(registry);
  CHECK_EQ(registry.on_query_result(14, QUERY_BATTERY, Outcome::Timeout) & CHANGE_REACH, 0);
  CHECK(registry.find(14)->reach == Reach::Reachable);
  CHECK((registry.on_query_result(14, QUERY_FAULT_DETAILS, Outcome::Timeout) & CHANGE_REACH) != 0);
  CHECK(registry.find(14)->reach == Reach::Unreachable);
  // Jede Nachricht der SID macht sie wieder erreichbar.
  CHECK((apply(registry, {0x71, 0x3D, 0xE4, 0x2F, 0x7C, 0x04, 0x05, 0x0E, 0x0B}) & CHANGE_REACH) != 0);
  CHECK(registry.find(14)->reach == Reach::Reachable);
  CHECK_EQ(registry.find(14)->missed, 0);

  // Eine Antwort dazwischen setzt den Zähler zurück.
  registry.on_query_result(14, QUERY_BATTERY, Outcome::Timeout);
  apply(registry, {0xC8, 0x3D, 0xE4, 0x2F, 0x06, 0x8A, 0x0B, 0x00, 0x00, 0x0E, 0x0C});
  registry.on_query_result(14, QUERY_BATTERY, Outcome::Timeout);
  CHECK(registry.find(14)->reach == Reach::Reachable);

  // D4 01 mit SID 25 in der Missing-Map.
  apply(registry, {0xD4, 0x03, 0x00, 0x40, 0x00, 0x02, 0x00, 0x00, 0x00, 0x00});
  CHECK((apply(registry, {0xD4, 0x01, 0x00, 0x00, 0x00, 0x00, 0x02, 0x00, 0x00, 0x00, 0x00}) & CHANGE_REACH) != 0);
  CHECK(registry.find(25)->reach == Reach::Unreachable);
  CHECK((registry.on_query_result(25, QUERY_REMOTE_STATUS, Outcome::Unreachable) & CHANGE_REACH) == 0);

  // Ein RM ohne Melder liefert D4 06, aber kein C4.
  apply(registry, {0xD4, 0x06, 0x19, 0x86, 0xBB, 0xBB, 0x70, 0xEF, 0x60, 0x1A, 0x03, 0x00, 0x00});
  const uint8_t changes = registry.on_query_result(25, QUERY_IDENTITY, Outcome::Timeout);
  CHECK((changes & CHANGE_IDENTITY) != 0);
  const Node *rm_only = registry.find(25);
  CHECK((rm_only->flags & NODE_TYPE_UNKNOWN) != 0);
  CHECK_EQ(rm_only->missed, 0);
  CHECK(rm_only->complete());
  CHECK(!rm_only->is_detector());
  CHECK_STR_CONTAINS(describe(*rm_only, 1000).c_str(), "RM ohne Melder");
  // Ein späteres C4 klärt den Typ.
  apply(registry, {0xC4, 0x60, 0x1A, 0x03, 0x82, 0x11, 0x04, 0x19, 0x01});
  CHECK((registry.find(25)->flags & NODE_TYPE_UNKNOWN) == 0);
  CHECK(registry.find(25)->is_detector());
  CHECK(!registry.find(25)->has_fault_events_support());

  // Ohne Platz bleibt das Ergebnis folgenlos.
  CHECK_EQ(registry.on_query_result(40, QUERY_REMOTE_STATUS, Outcome::Timeout), 0);
}

void test_device_change_and_gateway() {
  Registry registry;
  fill_sid14(registry);
  // Das RM von SID 14 steckt jetzt in einem anderen Melder.
  const uint8_t changes =
      apply(registry, {0xD4, 0x06, 0x0E, 0x64, 0x8C, 0x8C, 0x7D, 0xEF, 0x85, 0xD8, 0x33, 0x00, 0x00});
  CHECK((changes & CHANGE_DEVICE) != 0);
  const Node *node = registry.find(14);
  CHECK_EQ(node->device_id[0], 0x85);
  CHECK(!node->has_model());
  CHECK(!node->has_class());
  CHECK(!node->has_battery);
  CHECK(!node->complete());

  // Präsenzmeldung eines Gateway-RM.
  apply(registry, {0xD4, 0x09, 0x03, 0x01, 0x02, 0x03});
  CHECK(registry.find(3)->is_gateway());
  CHECK(registry.find(3)->complete());
  CHECK(!registry.find(3)->is_detector());
  // Beim Einlernen eines Gateways kam D4 09 00 01 02 03; SID 0 bleibt unberührt.
  apply(registry, {0xD4, 0x09, 0x00, 0x01, 0x02, 0x03});
  CHECK(registry.find(0) == nullptr);
  apply(registry, {0xD4, 0x09, 0x12, 0x5F, 0x4D, 0x35});
  CHECK_EQ(registry.find(18)->device_id[2], 0x35);
}

void test_alarm_state() {
  Registry registry;
  fill_sid14(registry);
  CHECK(registry.find(14)->alarm == AlarmState::Unknown);
  apply(registry, {0x50, 0x3D, 0xE4, 0x2F, 0x81, 0x00, 0x0E, 0x04});
  CHECK(registry.find(14)->alarm == AlarmState::Active);
  // 61 beendet den Alarm nicht und ändert die Klasse nicht.
  apply(registry, {0x61, 0x3D, 0xE4, 0x2F, 0x80, 0x01, 0x0E, 0x05});
  CHECK(registry.find(14)->alarm == AlarmState::Active);
  CHECK(registry.find(14)->silenced);
  CHECK_EQ(registry.find(14)->device_class, 0x81);
  CHECK_STR_CONTAINS(describe(*registry.find(14), 1000).c_str(), "ALARM");
  apply(registry, {0x51, 0x3D, 0xE4, 0x2F, 0x81, 0x0E, 0x06});
  CHECK(registry.find(14)->alarm == AlarmState::Ended);
  CHECK(!registry.find(14)->silenced);
}

void test_persistence() {
  Registry registry;
  apply(registry, {0xD4, 0x03, 0xFF, 0x7F, 0x1F, 0x05, 0x00, 0x00, 0x00, 0x00});
  fill_sid14(registry);
  apply(registry, {0xD4, 0x09, 0x03, 0x01, 0x02, 0x03});
  apply(registry, {0xD4, 0x06, 0x19, 0x86, 0xBB, 0xBB, 0x70, 0xEF, 0x60, 0x1A, 0x03, 0x00, 0x00});
  registry.on_query_result(25, QUERY_IDENTITY, Outcome::Timeout);
  const uint8_t own_id[] = {0xA5, 0xB8, 0x14};
  registry.set_own(20, own_id, MODEL_WST_630, 0xFF);

  uint8_t data[REGISTRY_FILE_LEN];
  CHECK_EQ(registry.serialize(data, sizeof(data) - 1), 0u);
  CHECK_EQ(registry.serialize(data, sizeof(data)), 408u);
  CHECK(std::memcmp(data, "W2NR", 4) == 0);
  CHECK_EQ(data[4], 1);
  // Nur Plätze mit Geräte-ID oder Flags: 3, 14, 20, 25.
  CHECK_EQ(data[5], 4);
  const uint8_t *slot14 = nullptr;
  for (size_t index = 0; index < REGISTRY_SLOTS; ++index) {
    if (data[8 + index * 8] == 14) {
      slot14 = data + 8 + index * 8;
    }
  }
  CHECK(slot14 != nullptr);
  if (slot14 != nullptr) {
    CHECK_EQ(slot14[1], 0);
    CHECK_EQ(slot14[2], 0x3D);
    CHECK_EQ(slot14[4], 0x2F);
    CHECK_EQ(slot14[5], 0x7C);
    CHECK_EQ(slot14[6], 0x04);
    CHECK_EQ(slot14[7], 0x81);
  }
  CHECK_EQ(data[8 + 4 * 8], 0xFF);

  Registry loaded;
  CHECK(loaded.deserialize(data, sizeof(data)) == LoadResult::Ok);
  CHECK_EQ(loaded.count(), 4u);
  CHECK(loaded.known(14));
  CHECK(loaded.known(3));
  CHECK(loaded.known(25));
  CHECK(loaded.find(20)->is_own());
  CHECK_EQ(loaded.find(14)->model, MODEL_ST_630_DE);
  CHECK(loaded.find(3)->is_gateway());
  CHECK((loaded.find(25)->flags & NODE_TYPE_UNKNOWN) != 0);
  // Zustände im RAM werden nicht gespeichert.
  CHECK(!loaded.find(14)->has_battery);
  CHECK(!loaded.find(14)->in_network);
  CHECK(loaded.find(14)->reach == Reach::Unknown);
  uint8_t again[REGISTRY_FILE_LEN];
  loaded.serialize(again, sizeof(again));
  CHECK(std::memcmp(data, again, sizeof(data)) == 0);

  uint8_t broken[REGISTRY_FILE_LEN];
  std::memcpy(broken, data, sizeof(data));
  broken[100] ^= 0x01;
  CHECK(loaded.deserialize(broken, sizeof(broken)) == LoadResult::BadCrc);
  CHECK_EQ(loaded.count(), 0u);
  CHECK(loaded.deserialize(data, sizeof(data) - 1) == LoadResult::BadLength);
  std::memcpy(broken, data, sizeof(data));
  broken[0] = 'X';
  CHECK(loaded.deserialize(broken, sizeof(broken)) == LoadResult::BadMagic);
  std::memcpy(broken, data, sizeof(data));
  broken[4] = 2;
  CHECK(loaded.deserialize(broken, sizeof(broken)) == LoadResult::BadVersion);
  std::memcpy(broken, data, sizeof(data));
  broken[5] = 5;
  CHECK(loaded.deserialize(broken, sizeof(broken)) == LoadResult::BadContent);

  // Doppelte SID mit gültiger CRC.
  std::memcpy(broken, data, sizeof(data));
  std::memcpy(broken + 8 + 4 * 8, broken + 8, 8);
  broken[5] = 5;
  const uint16_t crc = crc16_ccitt(broken + 8, 400);
  broken[6] = static_cast<uint8_t>(crc & 0xFF);
  broken[7] = static_cast<uint8_t>(crc >> 8);
  CHECK(loaded.deserialize(broken, sizeof(broken)) == LoadResult::BadContent);
  CHECK_EQ(loaded.count(), 0u);

  // Leeres Register: alle Plätze frei.
  Registry empty;
  empty.serialize(again, sizeof(again));
  CHECK_EQ(again[5], 0);
  CHECK(loaded.deserialize(again, sizeof(again)) == LoadResult::Ok);
  CHECK_EQ(loaded.count(), 0u);
}

void test_capacity() {
  Registry registry;
  uint8_t changes = 0;
  for (uint8_t sid = 0; sid < REGISTRY_SLOTS; ++sid) {
    CHECK(registry.get_or_add(sid, changes) != nullptr);
  }
  CHECK_EQ(registry.count(), REGISTRY_SLOTS);
  CHECK(registry.get_or_add(60, changes) == nullptr);
  CHECK(registry.get_or_add(64, changes) == nullptr);
  CHECK(registry.get_or_add(3, changes) != nullptr);
  registry.clear();
  CHECK_EQ(registry.count(), 0u);
  CHECK(registry.find(3) == nullptr);
}

// Log vom 12.10.2026: SID 24 meldet in D4 06 die Geräte-ID von SID 14 und beantwortet C3 nicht.
void test_shared_device_id() {
  Registry registry;
  apply(registry, {0xD4, 0x06, 0x18, 0x88, 0xC4, 0xC5, 0x2D, 0xEF, 0x3D, 0xE4, 0x2F, 0x01, 0x00});
  registry.on_query_result(24, QUERY_IDENTITY, Outcome::Timeout);
  const uint8_t id[] = {0x3D, 0xE4, 0x2F};
  CHECK(registry.find_device(id) == registry.find(24));
  fill_sid14(registry);
  CHECK(registry.find_device(id) == registry.find(14));
  CHECK(registry.known(24));
  CHECK(!registry.find(24)->is_detector());
  CHECK(registry.find(14)->is_detector());
}

}  // namespace

void run_registry_tests() {
  test_crc();
  test_detector_entry();
  test_requests_create_no_slot();
  test_membership_and_own();
  test_reachability();
  test_device_change_and_gateway();
  test_alarm_state();
  test_persistence();
  test_capacity();
  test_shared_device_id();
}
