#include "check.h"

#include "catalog.h"
#include "messages.h"
#include "node_view.h"
#include "registry.h"

#include <cstdlib>
#include <cstring>
#include <ctime>
#include <initializer_list>

using namespace esphome::wisafe2;

namespace {

RadioMessage radio(std::initializer_list<uint8_t> bytes) {
  Frame frame;
  for (uint8_t byte : bytes) {
    frame.data[frame.len++] = byte;
  }
  RadioMessage msg;
  CHECK(parse_radio(frame, msg));
  return msg;
}

void apply(Registry &registry, std::initializer_list<uint8_t> bytes) {
  Frame frame;
  for (uint8_t byte : bytes) {
    frame.data[frame.len++] = byte;
  }
  LocalMessage local;
  if (parse_local(frame, local)) {
    registry.apply(local, 1000);
    return;
  }
  RadioMessage msg;
  CHECK(parse_radio(frame, msg));
  registry.apply(msg, 1000);
}

// SID 14: ST-630-DE 2FE43D; SID 24: RM ohne Melder mit derselben gelernten ID (P14).
void prepare(Registry &registry) {
  apply(registry, {0xD4, 0x03, 0x00, 0x40, 0x00, 0x01, 0x00, 0x00, 0x00, 0x00});
  apply(registry, {0xC4, 0x3D, 0xE4, 0x2F, 0x81, 0x7C, 0x04, 0x0E, 0x05});
  apply(registry, {0xD4, 0x06, 0x18, 0x6E, 0x40, 0x40, 0x2D, 0xEF, 0x3D, 0xE4, 0x2F, 0x01, 0x00});
}

void test_states() {
  Registry registry;
  prepare(registry);
  const Node &node = *registry.find(14);
  CHECK(node_alarm(node) == Tri::Unknown);
  CHECK(node_fault(node) == Tri::Unknown);
  CHECK(node_socket_problem(node) == Tri::Unknown);
  CHECK(node_battery_low(node) == Tri::Unknown);
  CHECK(node_rm_battery_low(node) == Tri::Unknown);
  CHECK(node_online(node));

  // Abgenommen, kalibriert.
  apply(registry, {0x71, 0x3D, 0xE4, 0x2F, 0x7C, 0x04, 0x01, 0x0E, 0x0A});
  CHECK(node_socket_problem(node) == Tri::On);
  CHECK(node_fault(node) == Tri::Off);
  CHECK(node_battery_low(node) == Tri::Off);
  // Zurück auf dem Sockel.
  apply(registry, {0x71, 0x3D, 0xE4, 0x2F, 0x7C, 0x04, 0x05, 0x0E, 0x0B});
  CHECK(node_socket_problem(node) == Tri::Off);
  // 71 mit Melder-Batterie (0x08) und RM-Batterie (0x20).
  apply(registry, {0x71, 0x3D, 0xE4, 0x2F, 0x7C, 0x04, 0x2D, 0x0E, 0x0C});
  CHECK(node_battery_low(node) == Tri::On);
  CHECK(node_rm_battery_low(node) == Tri::On);
  CHECK(node_fault(node) == Tri::Off);
  apply(registry, {0x71, 0x3D, 0xE4, 0x2F, 0x7C, 0x04, 0x05, 0x0E, 0x0D});
  CHECK(node_battery_low(node) == Tri::Off);

  // 73 mit FS-Bit 2 (Melder-Batterie LOW): Fehler und schwache Batterie.
  apply(registry, {0x73, 0x3D, 0xE4, 0x2F, 0x7C, 0x04, 0x04, 0x00, 0x0E, 0x0E});
  CHECK(node_fault(node) == Tri::On);
  CHECK(node_battery_low(node) == Tri::On);
  CHECK(node_rm_battery_low(node) == Tri::Off);
  // FS-Bit 7: RM-Batterie LOW.
  apply(registry, {0x73, 0x3D, 0xE4, 0x2F, 0x7C, 0x04, 0x80, 0x00, 0x0E, 0x0F});
  CHECK(node_battery_low(node) == Tri::Off);
  CHECK(node_rm_battery_low(node) == Tri::On);
  apply(registry, {0x73, 0x3D, 0xE4, 0x2F, 0x7C, 0x04, 0x00, 0x00, 0x0E, 0x10});
  CHECK(node_fault(node) == Tri::Off);

  // Alarm: 61 beendet ihn nicht, erst 51.
  apply(registry, {0x50, 0x3D, 0xE4, 0x2F, 0x81, 0x00, 0x0E, 0x11});
  CHECK(node_alarm(node) == Tri::On);
  CHECK(registry.network_alarm() == AlarmState::Active);
  apply(registry, {0x61, 0x3D, 0xE4, 0x2F, 0x80, 0x00, 0x0E, 0x12});
  CHECK(node_alarm(node) == Tri::On);
  CHECK(registry.any_silenced());
  apply(registry, {0x51, 0x3D, 0xE4, 0x2F, 0x81, 0x0E, 0x13});
  CHECK(node_alarm(node) == Tri::Off);
  CHECK(registry.network_alarm() == AlarmState::Ended);
  CHECK(!registry.any_silenced());

  // Nicht erreichbar oder außerhalb der SID-Map: nicht im Netz.
  registry.on_query_result(14, QUERY_REMOTE_STATUS, Outcome::Unreachable);
  CHECK(!node_online(node));
  Registry empty;
  CHECK(empty.network_alarm() == AlarmState::Unknown);
  CHECK(!empty.any_silenced());
}

void test_resolve() {
  Registry registry;
  prepare(registry);
  const uint8_t id[] = {0x3D, 0xE4, 0x2F};
  const uint8_t other[] = {0x79, 0x11, 0x35};
  const uint8_t zero[] = {0x00, 0x00, 0x00};
  // Die Geräte-ID führt zum Melder an SID 14, nicht zum RM an SID 24.
  CHECK(registry.resolve(NO_SID, id) == registry.find(14));
  CHECK(registry.resolve(24, id) == registry.find(14));
  CHECK(registry.resolve(24, nullptr) == registry.find(24));
  CHECK(registry.resolve(24, zero) == registry.find(24));
  // Unbekannte Geräte-ID: die SID gilt nur ohne andere Geräte-ID.
  CHECK(registry.resolve(14, other) == nullptr);
  CHECK(registry.resolve(NO_SID, other) == nullptr);
  apply(registry, {0xD4, 0x03, 0x00, 0x00, 0x01, 0x00, 0x00, 0x00, 0x00, 0x00});
  CHECK(registry.resolve(16, other) == registry.find(16));
  CHECK(registry.resolve(30, nullptr) == nullptr);
}

void test_texts() {
  Registry registry;
  prepare(registry);
  char text[160];
  describe_model(*registry.find(14), text, sizeof(text));
  CHECK(std::strcmp(text, "ST-630-DE, 2FE43D") == 0);
  describe_model(*registry.find(24), text, sizeof(text));
  CHECK(std::strcmp(text, "Modell unbekannt, 2FE43D") == 0);

  describe_fault_details(MODEL_ST_630_DE, 0x0000, text, sizeof(text));
  CHECK(std::strcmp(text, "keine Fehler") == 0);
  describe_fault_details(MODEL_ST_630_DE, 0x0084, text, sizeof(text));
  CHECK(std::strcmp(text, "Melder-Batterie (LOW), RM-Batterie (LOW)") == 0);
  describe_fault_details(MODEL_ST_630_DE, 0x8000, text, sizeof(text));
  CHECK(std::strcmp(text, "Bit 15") == 0);
  // Ein kleiner Puffer wird sauber abgeschnitten.
  char small[8];
  describe_fault_details(MODEL_ST_630_DE, 0x0084, small, sizeof(small));
  CHECK_EQ(std::strlen(small), 7);

  CHECK(std::strcmp(event_type(RadioType::Alarm), "alarm") == 0);
  CHECK(std::strcmp(event_type(RadioType::AlarmStop), "alarm_ende") == 0);
  CHECK(std::strcmp(event_type(RadioType::Silence), "stummschaltung") == 0);
  CHECK(std::strcmp(event_type(RadioType::Test), "test") == 0);
  CHECK(std::strcmp(event_type(RadioType::Fault), "fehlerbericht") == 0);
  CHECK(event_type(RadioType::Battery) == nullptr);

  describe_event(radio({0x50, 0x3D, 0xE4, 0x2F, 0x81, 0x00, 0x0E, 0x11}), "Flur", text, sizeof(text));
  CHECK(std::strcmp(text, "Alarm: SID 14 (Flur), Rauch") == 0);
  describe_event(radio({0x51, 0x3D, 0xE4, 0x2F, 0x81, 0x0E, 0x13}), nullptr, text, sizeof(text));
  CHECK(std::strcmp(text, "Alarmende: SID 14") == 0);
  describe_event(radio({0x61, 0x3D, 0xE4, 0x2F, 0x80, 0x00, 0x0E, 0x12}), "", text, sizeof(text));
  CHECK(std::strcmp(text, "Stummschaltung: SID 14, Feuer") == 0);
  describe_event(radio({0x70, 0x3D, 0xE4, 0x2F, 0x81, 0x01, 0x7C, 0x04, 0x0E, 0x14}), "Flur", text, sizeof(text));
  CHECK(std::strcmp(text, "Test bestanden: SID 14 (Flur)") == 0);
  const RadioMessage request = radio({0x70, 0x01, 0x02, 0x03, 0x81, 0x01, 0x00, 0x00, 0x0F, 0x0B});
  CHECK(is_test_request(request));
  CHECK(!is_test_request(radio({0x70, 0x3D, 0xE4, 0x2F, 0x81, 0x01, 0x7C, 0x04, 0x0E, 0x14})));
  CHECK(!is_test_request(radio({0x71, 0x01, 0x02, 0x03, 0x00, 0x00, 0x05, 0x0F, 0x03})));
  describe_event(request, nullptr, text, sizeof(text));
  CHECK(std::strcmp(text, "Testanforderung des W2-MM: SID 15") == 0);
  describe_event(request, "Flur", text, sizeof(text));
  CHECK(std::strcmp(text, "Testanforderung des W2-MM: SID 15") == 0);
  describe_event(radio({0x71, 0x3D, 0xE4, 0x2F, 0x7C, 0x04, 0x0B, 0x0E, 0x15}), "Flur", text, sizeof(text));
  CHECK(std::strcmp(text, "Fehlerbericht: SID 14 (Flur), abgenommen, fehlerhaft, Melder-Batterie") == 0);
  describe_event(radio({0xC8, 0x3D, 0xE4, 0x2F, 0x06, 0x8A, 0x0B, 0x00, 0x00, 0x0E, 0x08}), "Flur", text,
                 sizeof(text));
  CHECK(text[0] == '\0');
}

void test_time_texts() {
  setenv("TZ", "UTC0", 1);
  tzset();
  char text[64];
  CHECK(!epoch_valid(0));
  CHECK(epoch_valid(1791900180));
  format_timestamp(1791900180, text, sizeof(text));
  CHECK(std::strcmp(text, "2026-10-13T14:03:00Z") == 0);
  describe_test(0x01, 1791900180, text, sizeof(text));
  CHECK(std::strcmp(text, "bestanden am 13.10. 14:03") == 0);
  describe_test(0x00, 0, text, sizeof(text));
  CHECK(std::strcmp(text, "fehlgeschlagen") == 0);
  char small[8];
  format_timestamp(1791900180, small, sizeof(small));
  CHECK(small[0] == '\0');
}

void test_template() {
  Registry registry;
  prepare(registry);
  char text[64];
  const Node &detector = *registry.find(14);
  CHECK(node_assignable(detector));
  CHECK(template_by_device(registry, detector));
  template_name(detector, true, text, sizeof(text));
  CHECK(std::strcmp(text, "ST-630-DE 2FE43D") == 0);
  template_device_key(detector, true, text, sizeof(text));
  CHECK(std::strcmp(text, "alarm_2FE43D") == 0);
  // SID 24 führt dieselbe Geräte-ID ohne Modell (P14): Die Vorlage nennt sie über die SID.
  const Node &rm = *registry.find(24);
  CHECK(!template_by_device(registry, rm));
  template_name(rm, false, text, sizeof(text));
  CHECK(std::strcmp(text, "Melder SID 24") == 0);
  template_device_key(rm, false, text, sizeof(text));
  CHECK(std::strcmp(text, "alarm_sid_24") == 0);

  apply(registry, {0xD4, 0x03, 0x00, 0x00, 0x01, 0x00, 0x00, 0x00, 0x00, 0x00});
  const Node &unknown = *registry.find(16);
  CHECK(!template_by_device(registry, unknown));
  // Ohne Geräte-ID ist offen, ob an SID 16 ein Melder sitzt; sie gehört nicht in die Vorlage.
  CHECK(!node_assignable(unknown));
  // WHT-630 132F08 an SID 3 aus C4.
  apply(registry, {0xC4, 0x08, 0x2F, 0x13, 0x82, 0x11, 0x04, 0x03, 0x05});
  const Node &heat = *registry.find(3);
  CHECK(template_by_device(registry, heat));
  template_name(heat, true, text, sizeof(text));
  CHECK(std::strcmp(text, "WHT-630 132F08") == 0);
  CHECK(std::strcmp(alarm_device_class(heat.device_class), "heat") == 0);

  CHECK(std::strcmp(alarm_device_class(0x81), "smoke") == 0);
  CHECK(std::strcmp(alarm_device_class(0x82), "heat") == 0);
  CHECK(std::strcmp(alarm_device_class(0x41), "carbon_monoxide") == 0);
  CHECK(alarm_device_class(0xC9) == nullptr);

  describe_unassigned(registry, 0, text, sizeof(text));
  CHECK(std::strcmp(text, "SID 3, 14, 24") == 0);
  char small[16];
  describe_unassigned(registry, 0, small, sizeof(small));
  CHECK(std::strcmp(small, "SID 3, 14, …") == 0);
  describe_unassigned(registry, (uint64_t{1} << 3) | (uint64_t{1} << 14), text, sizeof(text));
  CHECK(std::strcmp(text, "SID 24") == 0);
  // Ein RM ohne Melder gehört nicht in die YAML.
  registry.on_query_result(24, QUERY_IDENTITY, Outcome::Timeout);
  CHECK(!node_assignable(*registry.find(24)));
  describe_unassigned(registry, (uint64_t{1} << 3) | (uint64_t{1} << 14), text, sizeof(text));
  CHECK(std::strcmp(text, "keine") == 0);
}

}  // namespace

void run_node_view_tests() {
  test_states();
  test_resolve();
  test_texts();
  test_time_texts();
  test_template();
}
