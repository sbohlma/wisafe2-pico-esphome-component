#include "check.h"

#include "catalog.h"
#include "discovery.h"
#include "messages.h"
#include "registry.h"

#include <initializer_list>

using namespace esphome::wisafe2;

namespace {

constexpr uint8_t OWN_SID = 20;

Frame make_frame(std::initializer_list<uint8_t> bytes) {
  Frame frame;
  for (uint8_t byte : bytes) {
    frame.data[frame.len++] = byte;
  }
  return frame;
}

void apply(Registry &registry, std::initializer_list<uint8_t> bytes, uint32_t now = 1000) {
  const Frame frame = make_frame(bytes);
  LocalMessage local;
  if (parse_local(frame, local)) {
    registry.apply(local, now);
    return;
  }
  RadioMessage radio;
  CHECK(parse_radio(frame, radio));
  registry.apply(radio, now);
}

// SIDs 3, 14 und 20 im Netz; 14 ist vollständig bekannt (ST-630-DE), 20 ist die Brücke.
void prepare(Registry &registry) {
  apply(registry, {0xD4, 0x03, 0x08, 0x40, 0x10, 0x00, 0x00, 0x00, 0x00, 0x00});
  apply(registry, {0xC4, 0x3D, 0xE4, 0x2F, 0x81, 0x7C, 0x04, 0x0E, 0x05});
  const uint8_t own_id[] = {0xA5, 0xB8, 0x14};
  registry.set_own(OWN_SID, own_id, MODEL_WST_630, 0xFF);
}

constexpr uint64_t MAP = (uint64_t{1} << 3) | (uint64_t{1} << 14) | (uint64_t{1} << OWN_SID);

bool expect_next(Discovery &discovery, const Registry &registry, uint8_t sid, uint8_t query) {
  DiscoveryTask task;
  if (!discovery.next(registry, task)) {
    std::printf("  keine Aufgabe, erwartet SID %u %s\n", sid, query_name(query));
    return false;
  }
  if (task.sid != sid || task.query != query) {
    std::printf("  Aufgabe SID %u %s, erwartet SID %u %s\n", task.sid, query_name(task.query), sid, query_name(query));
    return false;
  }
  return true;
}

void test_query_for_payload() {
  const uint8_t status[] = {0xD3, 0x06, 0x0E, 0x00};
  const uint8_t identity[] = {0xD3, 0x06, 0x0E, 0x01};
  const uint8_t map[] = {0xD3, 0x03};
  const uint8_t battery[] = {0xC7, 0x3D, 0xE4, 0x2F, 0x7C, 0x04, 0x06};
  CHECK_EQ(query_for_payload(status, sizeof(status)), QUERY_REMOTE_STATUS);
  CHECK_EQ(query_for_payload(identity, sizeof(identity)), QUERY_IDENTITY);
  CHECK_EQ(query_for_payload(map, sizeof(map)), 0);
  CHECK_EQ(query_for_payload(battery, sizeof(battery)), QUERY_BATTERY);
  const uint8_t details = 0x72;
  const uint8_t events = 0xC5;
  CHECK_EQ(query_for_payload(&details, 1), QUERY_FAULT_DETAILS);
  CHECK_EQ(query_for_payload(&events, 1), QUERY_FAULT_EVENTS);
  CHECK_EQ(query_for_payload(nullptr, 0), 0);
}

void test_startup_missing_only() {
  Registry registry;
  prepare(registry);
  Discovery discovery;
  CHECK(!discovery.has_work());
  discovery.request(Discovery::Scope::Missing);
  CHECK(discovery.waiting_for_map());
  // Nur SID 3 hat keinen Eintrag.
  CHECK_EQ(discovery.on_map(MAP, registry, OWN_SID), 1);
  CHECK(!discovery.waiting_for_map());
  CHECK(discovery.campaign_running());
  CHECK_EQ(discovery.campaign_total(), 1);

  CHECK(expect_next(discovery, registry, 3, QUERY_REMOTE_STATUS));
  CHECK_EQ(discovery.campaign_index(), 1);
  DiscoveryTask task;
  // Höchstens eine offene Abfrage.
  CHECK(!discovery.next(registry, task));
  CHECK(discovery.busy());
  // Ein fremdes Ergebnis beendet die offene Abfrage nicht.
  discovery.on_result(3, QUERY_IDENTITY, Outcome::Ok);
  CHECK(discovery.busy());

  apply(registry, {0xD4, 0x06, 0x03, 0x64, 0x8C, 0x8C, 0x60, 0xEF, 0x08, 0x2F, 0x13, 0x00, 0x00});
  discovery.on_result(3, QUERY_REMOTE_STATUS, Outcome::Ok);
  CHECK(expect_next(discovery, registry, 3, QUERY_IDENTITY));
  // C4 des Hitzemelders WHT-630.
  apply(registry, {0xC4, 0x08, 0x2F, 0x13, 0x82, 0x11, 0x04, 0x03, 0x02});
  discovery.on_result(3, QUERY_IDENTITY, Outcome::Ok);
  CHECK(expect_next(discovery, registry, 3, QUERY_BATTERY));
  discovery.on_result(3, QUERY_BATTERY, Outcome::Ok);
  CHECK(expect_next(discovery, registry, 3, QUERY_FAULT_DETAILS));
  CHECK(!discovery.take_finished());
  discovery.on_result(3, QUERY_FAULT_DETAILS, Outcome::Ok);
  // C5 nur bei WST-630 und ST-630-DE.
  CHECK(!discovery.next(registry, task));
  CHECK(discovery.take_finished());
  CHECK(!discovery.take_finished());
  CHECK(!discovery.campaign_running());
  CHECK(registry.known(3));

  // Mit vollständigem Register gibt es beim nächsten Start nichts zu tun.
  discovery.request(Discovery::Scope::Missing);
  CHECK_EQ(discovery.on_map(MAP, registry, OWN_SID), 0);
  CHECK(!discovery.has_work());
  CHECK(!discovery.campaign_running());
  // Ohne Anforderung bleibt eine SID-Map folgenlos.
  CHECK_EQ(discovery.on_map(MAP, registry, OWN_SID), 0);
}

void test_full_discovery() {
  Registry registry;
  prepare(registry);
  Discovery discovery;
  discovery.request(Discovery::Scope::All);
  discovery.request(Discovery::Scope::Missing);
  CHECK_EQ(discovery.on_map(MAP, registry, OWN_SID), 2);
  CHECK_EQ(discovery.campaign_total(), 2);

  // SID 3 antwortet nicht: die übrigen Schritte entfallen.
  CHECK(expect_next(discovery, registry, 3, QUERY_REMOTE_STATUS));
  discovery.on_result(3, QUERY_REMOTE_STATUS, Outcome::Unreachable);

  // SID 14 hat eine bekannte Klasse, C4 entfällt.
  CHECK(expect_next(discovery, registry, 14, QUERY_REMOTE_STATUS));
  CHECK_EQ(discovery.campaign_index(), 2);
  discovery.on_result(14, QUERY_REMOTE_STATUS, Outcome::Ok);
  CHECK(expect_next(discovery, registry, 14, QUERY_BATTERY));
  // Eine ausgebliebene Antwort beendet die Schritte nicht.
  discovery.on_result(14, QUERY_BATTERY, Outcome::Timeout);
  CHECK(expect_next(discovery, registry, 14, QUERY_FAULT_DETAILS));
  discovery.on_result(14, QUERY_FAULT_DETAILS, Outcome::Ok);
  CHECK(expect_next(discovery, registry, 14, QUERY_FAULT_EVENTS));
  CHECK(!discovery.take_finished());
  discovery.on_result(14, QUERY_FAULT_EVENTS, Outcome::Ok);
  CHECK(discovery.take_finished());
  CHECK(!discovery.has_work());
}

void test_rm_without_detector() {
  Registry registry;
  prepare(registry);
  Discovery discovery;
  discovery.request(Discovery::Scope::Missing);
  CHECK_EQ(discovery.on_map(MAP, registry, OWN_SID), 1);
  CHECK(expect_next(discovery, registry, 3, QUERY_REMOTE_STATUS));
  apply(registry, {0xD4, 0x06, 0x03, 0x2A, 0x38, 0x41, 0x30, 0xEF, 0x60, 0x1A, 0x03, 0x00, 0x00});
  discovery.on_result(3, QUERY_REMOTE_STATUS, Outcome::Ok);
  CHECK(expect_next(discovery, registry, 3, QUERY_IDENTITY));
  registry.on_query_result(3, QUERY_IDENTITY, Outcome::Timeout);
  discovery.on_result(3, QUERY_IDENTITY, Outcome::Timeout);
  DiscoveryTask task;
  CHECK(!discovery.next(registry, task));
  CHECK(discovery.take_finished());
  CHECK(registry.known(3));
}

void test_rejected_and_events() {
  Registry registry;
  prepare(registry);
  Discovery discovery;
  // Nach 71 mit Flag 0x02: 72 und C5, ohne Erkundung.
  discovery.want(14, QUERY_FAULT_DETAILS | QUERY_FAULT_EVENTS);
  CHECK(discovery.has_work());
  CHECK(!discovery.campaign_running());
  CHECK(expect_next(discovery, registry, 14, QUERY_FAULT_DETAILS));
  // 47: SID nicht in der lokalen SID-Map, die übrigen Schritte entfallen.
  discovery.on_result(14, QUERY_FAULT_DETAILS, Outcome::Rejected);
  CHECK(!discovery.has_work());
  CHECK(!discovery.take_finished());

  // Die eigene SID wird nie abgefragt, auch nicht mit want.
  discovery.want(OWN_SID, QUERY_IDENTITY | QUERY_BATTERY);
  DiscoveryTask task;
  CHECK(!discovery.next(registry, task));
  discovery.want(70, QUERY_ALL);
  CHECK(!discovery.has_work());

  discovery.want(3, QUERY_ALL);
  discovery.clear();
  CHECK(!discovery.has_work());
  CHECK(!discovery.busy());
}

void test_periodic() {
  Registry registry;
  prepare(registry);
  apply(registry, {0xD4, 0x06, 0x03, 0x64, 0x8C, 0x8C, 0x60, 0xEF, 0x08, 0x2F, 0x13, 0x00, 0x00});
  apply(registry, {0xC4, 0x08, 0x2F, 0x13, 0x82, 0x11, 0x04, 0x03, 0x02});
  Discovery discovery;
  const uint32_t status_ms = 19u * 3600u * 1000u;
  const uint32_t battery_ms = 24u * 3600u * 1000u;
  discovery.set_intervals(status_ms, battery_ms);
  // Kurz vor dem Überlauf von millis().
  const uint32_t start = 0xFFFFF000u;
  discovery.tick(start, registry, OWN_SID);
  CHECK(!discovery.has_work());
  discovery.tick(start + PERIODIC_START_DELAY_MS - 1, registry, OWN_SID);
  CHECK(!discovery.has_work());

  uint32_t now = start + PERIODIC_START_DELAY_MS;
  discovery.tick(now, registry, OWN_SID);
  // Remote-Status und Batterie für den ersten Knoten, die Brücke ausgenommen.
  CHECK(expect_next(discovery, registry, 3, QUERY_REMOTE_STATUS));
  discovery.on_result(3, QUERY_REMOTE_STATUS, Outcome::Ok);
  CHECK(expect_next(discovery, registry, 3, QUERY_BATTERY));
  discovery.on_result(3, QUERY_BATTERY, Outcome::Ok);
  CHECK(!discovery.has_work());
  CHECK(!discovery.take_finished());

  // Zwei Knoten: der nächste Remote-Status nach 19 h / 2.
  discovery.tick(now + status_ms / 2 - 1, registry, OWN_SID);
  CHECK(!discovery.has_work());
  now += status_ms / 2;
  discovery.tick(now, registry, OWN_SID);
  CHECK(expect_next(discovery, registry, 14, QUERY_REMOTE_STATUS));
  discovery.on_result(14, QUERY_REMOTE_STATUS, Outcome::Ok);
  CHECK(!discovery.has_work());

  // Batterie nach 24 h / 2 für SID 14; ein nicht erreichbarer Knoten entfällt.
  registry.on_query_result(3, QUERY_REMOTE_STATUS, Outcome::Unreachable);
  now = start + PERIODIC_START_DELAY_MS + battery_ms / 2;
  discovery.tick(now, registry, OWN_SID);
  CHECK(expect_next(discovery, registry, 14, QUERY_BATTERY));
  discovery.on_result(14, QUERY_BATTERY, Outcome::Ok);
  CHECK(!discovery.has_work());

  // Der Remote-Status fragt auch nicht erreichbare Knoten weiter ab.
  now = start + PERIODIC_START_DELAY_MS + status_ms;
  discovery.tick(now, registry, OWN_SID);
  CHECK(expect_next(discovery, registry, 3, QUERY_REMOTE_STATUS));
  discovery.on_result(3, QUERY_REMOTE_STATUS, Outcome::Unreachable);
  CHECK(!discovery.has_work());

  // Während einer Erkundung pausiert die periodische Planung.
  discovery.request(Discovery::Scope::All);
  discovery.tick(now + battery_ms, registry, OWN_SID);
  CHECK(!discovery.has_work());

  Discovery off;
  off.tick(0, registry, OWN_SID);
  off.tick(PERIODIC_START_DELAY_MS * 10, registry, OWN_SID);
  CHECK(!off.has_work());
}

}  // namespace

void run_discovery_tests() {
  test_query_for_payload();
  test_startup_missing_only();
  test_full_discovery();
  test_rm_without_detector();
  test_rejected_and_events();
  test_periodic();
}
