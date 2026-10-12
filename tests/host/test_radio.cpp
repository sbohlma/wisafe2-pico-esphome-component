#include "check.h"

#include "catalog.h"
#include "duplicate_filter.h"
#include "frame_analyzer.h"
#include "messages.h"
#include "node_cache.h"

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

std::string describe(std::initializer_list<uint8_t> bytes) {
  char text[256];
  describe_frame(make_frame(bytes), text, sizeof(text));
  return text;
}

#define CHECK_DESCRIBES(needle, ...) CHECK_STR_CONTAINS(describe({__VA_ARGS__}).c_str(), needle)

void test_catalog() {
  const uint8_t wht[] = {0x11, 0x04};
  CHECK_EQ(read_model(wht), 1041);
  uint8_t packed[2];
  write_model(MODEL_WST_630, packed);
  CHECK_EQ(packed[0], 0x11);
  CHECK_EQ(packed[1], 0x03);
  CHECK(std::strcmp(model_name(1041), "WHT-630") == 0);
  CHECK(std::strcmp(model_name(1219), "W2-SVP-630") == 0);
  CHECK(std::strcmp(model_name(2285), "FP2620W2") == 0);
  CHECK(model_name(4711) == nullptr);
  CHECK(std::strcmp(class_name(0x81), "Rauch") == 0);
  CHECK(std::strcmp(class_name(0xC9), "Gateway") == 0);
  CHECK(class_name(0x13) == nullptr);
  CHECK(std::strcmp(silence_class_name(0x40), "CO") == 0);
  CHECK(std::strcmp(fault_detail_name(MODEL_WST_630, 4), "Kammer (Degraded)") == 0);
  CHECK(std::strcmp(fault_detail_name(MODEL_WHT_630, 4), "Thermistor (Degraded)") == 0);
  CHECK(std::strcmp(fault_detail_name(MODEL_WHM_1, 14), "Sirene (Failed)") == 0);
  CHECK(std::strcmp(fault_detail_name(MODEL_W2_CO_10X, 13), "Lebensende (EOL)") == 0);
  // Unbekannte Modelle wie Rauch.
  CHECK(std::strcmp(fault_detail_name(4711, 12), "Thermoptek (Failed)") == 0);
  CHECK(fault_detail_name(MODEL_WHT_630, 1) == nullptr);
  CHECK(fault_detail_name(MODEL_WHT_630, 16) == nullptr);
  const uint8_t gateway[] = {0x01, 0x02, 0x03};
  CHECK(is_gateway_id(gateway));
}

void test_parse_examples() {
  RadioMessage msg;
  // Testtaste SID 14, Rauch, OK, ST-630-DE, seq 3.
  CHECK(parse_radio(make_frame({0x70, 0x3D, 0xE4, 0x2F, 0x81, 0x01, 0x7C, 0x04, 0x0E, 0x03}), msg));
  CHECK(msg.type == RadioType::Test);
  CHECK_EQ(msg.sid, 14);
  CHECK_EQ(msg.seq, 3);
  CHECK_EQ(msg.model, MODEL_ST_630_DE);
  CHECK_EQ(msg.device_class, 0x81);
  CHECK_EQ(msg.value, 0x01);

  CHECK(parse_radio(make_frame({0x73, 0xDC, 0xF0, 0x33, 0x7C, 0x04, 0x00, 0x00, 0x0B, 0x0C}), msg));
  CHECK(msg.type == RadioType::FaultDetails);
  CHECK_EQ(msg.value16, 0);
  CHECK_EQ(msg.sid, 11);

  CHECK(parse_radio(make_frame({0xC8, 0xDC, 0xF0, 0x33, 0x06, 0x8A, 0x0B, 0x00, 0x00, 0x0B, 0x0D}), msg));
  CHECK(msg.type == RadioType::Battery);
  CHECK_EQ(msg.value16, 2954);
  CHECK_EQ(msg.value, 0x06);
  CHECK_EQ(msg.seq, 13);

  CHECK(parse_radio(make_frame({0xC2, 0x08, 0x2F, 0x13, 0x82, 0x11, 0x04, 0x01, 0x03, 0x0C}), msg));
  CHECK(msg.type == RadioType::DiagnoseResult);
  CHECK_EQ(msg.model, MODEL_WHT_630);
  CHECK_EQ(msg.sid, 3);

  CHECK(parse_radio(make_frame({0xC4, 0xA5, 0xB8, 0x13, 0xFF, 0x11, 0x03, 0x10, 0x02}), msg));
  CHECK(msg.type == RadioType::IdInfo);
  CHECK_EQ(msg.device_class, 0xFF);
  CHECK_EQ(msg.model, MODEL_WST_630);
  CHECK_EQ(msg.sid, 16);

  // Anfrage mit zusätzlichem 7E vor SID und seq.
  CHECK(parse_radio(make_frame({0x72, 0x3D, 0xE4, 0x2F, 0x7C, 0x04, 0x00, 0x7E, 0x0F, 0x04}), msg));
  CHECK(msg.type == RadioType::FaultDetailsRequest);
  CHECK(is_host_request(msg.type));
  CHECK_EQ(msg.sid, 15);
  CHECK_EQ(msg.seq, 4);
  CHECK_EQ(msg.device_id[2], 0x2F);

  CHECK(parse_radio(make_frame({0xC3, 0x00, 0x00, 0x00, 0x05, 0x02}), msg));
  CHECK(msg.type == RadioType::IdRequest);
  CHECK_EQ(msg.sid, 5);

  CHECK(parse_radio(make_frame({0xC1, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x01, 0x05, 0x02}), msg));
  CHECK(msg.type == RadioType::DiagnoseRequest);
  CHECK_EQ(msg.value, 0x01);

  CHECK(parse_radio(make_frame({0xC7, 0xA5, 0xB8, 0x14, 0x11, 0x03, 0x06, 0x05, 0x04}), msg));
  CHECK(msg.type == RadioType::BatteryRequest);
  CHECK_EQ(msg.value, 0x06);

  CHECK(parse_radio(make_frame({0xC6, 0xDC, 0xF0, 0x33, 0x74, 0x01, 0x02, 0x03, 0x00, 0x0B, 0x0E}), msg));
  CHECK(msg.type == RadioType::FaultEvents);
  CHECK_EQ(msg.events[2], 0x03);

  CHECK(parse_radio(make_frame({0x61, 0x3D, 0xE4, 0x2F, 0x80, 0x01, 0x0E, 0x07}), msg));
  CHECK(msg.type == RadioType::Silence);

  // Falsche Längen und unbekannte Typen.
  CHECK(!parse_radio(make_frame({0x70, 0x3D, 0xE4}), msg));
  CHECK(!parse_radio(make_frame({0x70, 0x3D, 0xE4, 0x2F, 0x81, 0x01, 0x7C, 0x04, 0x7E, 0x0E, 0x03}), msg));
  CHECK(!parse_radio(make_frame({0x22, 0x05}), msg));
  CHECK(!parse_radio(make_frame({0xD2, 0x00}), msg));
}

void test_describe_radio() {
  CHECK_DESCRIBES("Fehlerdetails Gerät 33F0DC, Modell 1148 ST-630-DE, FS 0000 (keine Fehler), SID 11, Folge 12",
                  0x73, 0xDC, 0xF0, 0x33, 0x7C, 0x04, 0x00, 0x00, 0x0B, 0x0C);
  CHECK_DESCRIBES("Modell 1041 WHT-630, FS 0004: Melder-Batterie (LOW), SID 3",
                  0x73, 0x08, 0x2F, 0x13, 0x11, 0x04, 0x04, 0x00, 0x03, 0x01);
  CHECK_DESCRIBES("FS 2102: Temperaturdiode1 (Failed), SPI-Puffer (Overflow), Lebensende (EOL)",
                  0x73, 0x08, 0x2F, 0x13, 0x78, 0x03, 0x02, 0x21, 0x03, 0x01);
  CHECK_DESCRIBES("Batterie Gerät 33F0DC, 2954 mV, SID 11, Folge 13",
                  0xC8, 0xDC, 0xF0, 0x33, 0x06, 0x8A, 0x0B, 0x00, 0x00, 0x0B, 0x0D);
  CHECK_DESCRIBES("Fehlerereignisse Gerät 33F0DC, 00 00 00 (keine), SID 11",
                  0xC6, 0xDC, 0xF0, 0x33, 0x74, 0x00, 0x00, 0x00, 0x00, 0x0B, 0x0E);
  CHECK_DESCRIBES("Diagnoseergebnis Gerät 132F08, Hitze, Modell 1041 WHT-630, bestanden, SID 3, Folge 12",
                  0xC2, 0x08, 0x2F, 0x13, 0x82, 0x11, 0x04, 0x01, 0x03, 0x0C);
  CHECK_DESCRIBES("ID-Information Gerät 13B8A5, Blitz/Vibration (alle Alarme), Modell 785 WST-630, SID 16",
                  0xC4, 0xA5, 0xB8, 0x13, 0xFF, 0x11, 0x03, 0x10, 0x02);
  CHECK_DESCRIBES("Gerät 030201 (Gateway), Gateway, Modell 4660 (unbekannt)",
                  0xC4, 0x01, 0x02, 0x03, 0xC9, 0x34, 0x12, 0x02, 0x01);
  CHECK_DESCRIBES("Fehlerbericht Gerät 018D2D, Modell 2285 FP2620W2, Flags 43: abgenommen, kalibriert, fehlerhaft, "
                  "Bit 0x40 (schwache Batterie), SID 14, Folge 5",
                  0x71, 0x2D, 0x8D, 0x01, 0xED, 0x08, 0x43, 0x0E, 0x05);
  CHECK_DESCRIBES("Flags 05: auf Sockel, kalibriert",
                  0x71, 0x2D, 0x8D, 0x01, 0x7C, 0x04, 0x05, 0x0E, 0x06);
  CHECK_DESCRIBES("Alarm Gerät 2FE43D, Rauch, d5 00, SID 14",
                  0x50, 0x3D, 0xE4, 0x2F, 0x81, 0x00, 0x0E, 0x05);
  CHECK_DESCRIBES("Alarm Ende Gerät 2FE43D, CO, SID 14, Folge 6",
                  0x51, 0x3D, 0xE4, 0x2F, 0x41, 0x0E, 0x06);
  CHECK_DESCRIBES("Stummschaltung Gerät 2FE43D, Feuer, d5 01",
                  0x61, 0x3D, 0xE4, 0x2F, 0x80, 0x01, 0x0E, 0x07);
  CHECK_DESCRIBES("Anfrage Fehlerdetails an Gerät 2FE43D, Modell 1148 ST-630-DE, von SID 15, Folge 4",
                  0x72, 0x3D, 0xE4, 0x2F, 0x7C, 0x04, 0x00, 0x7E, 0x0F, 0x04);
  CHECK_DESCRIBES("Anfrage Batterie an Gerät 14B8A5",
                  0xC7, 0xA5, 0xB8, 0x14, 0x11, 0x03, 0x06, 0x05, 0x04);
  CHECK_DESCRIBES("Anfrage Fehlerereignisse an Gerät",
                  0xC5, 0xA5, 0xB8, 0x14, 0x11, 0x03, 0x74, 0x05, 0x04);
  CHECK_DESCRIBES("ID-Anfrage von SID 5, Folge 2",
                  0xC3, 0x00, 0x00, 0x00, 0x05, 0x02);
  CHECK_DESCRIBES("Diagnoseanfrage von SID 5",
                  0xC1, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x01, 0x05, 0x02);
  CHECK_DESCRIBES("D5 Gerät 2FE43D, Modell 1148 ST-630-DE, d6 14",
                  0xD5, 0x3D, 0xE4, 0x2F, 0x7C, 0x04, 0x14, 0x09, 0x01);
}

void test_duplicates() {
  DuplicateFilter filter;
  CHECK(!filter.seen(11, 0xC8, 13, 1000));
  CHECK(filter.seen(11, 0xC8, 13, 1001));
  CHECK(filter.seen(11, 0xC8, 13, 60999));
  CHECK(!filter.seen(11, 0xC8, 14, 2000));
  CHECK(!filter.seen(12, 0xC8, 13, 2000));
  CHECK(!filter.seen(11, 0x73, 13, 2000));
  // Nach 60 s gilt dieselbe Nachricht wieder als neu.
  CHECK(!filter.seen(11, 0xC8, 13, 61000));
  filter.clear();
  CHECK(!filter.seen(11, 0xC8, 14, 61001));
}

void test_node_cache() {
  NodeCache cache;
  RadioMessage msg;
  CHECK(parse_radio(make_frame({0xC4, 0xA5, 0xB8, 0x13, 0xFF, 0x11, 0x03, 0x10, 0x02}), msg));
  cache.update(msg);
  const NodeInfo *node = cache.get(16);
  CHECK(node != nullptr);
  CHECK(node->has_device);
  CHECK(node->has_model);
  CHECK_EQ(node->model, MODEL_WST_630);
  CHECK_EQ(node->device_class, 0xFF);

  // Anfragen tragen das Zielgerät und ändern den Absender nicht.
  CHECK(parse_radio(make_frame({0xC7, 0x3D, 0xE4, 0x2F, 0x7C, 0x04, 0x06, 0x10, 0x03}), msg));
  cache.update(msg);
  CHECK_EQ(cache.get(16)->device_id[0], 0xA5);

  LocalMessage local;
  CHECK(parse_local(make_frame({0xD4, 0x06, 0x18, 0x86, 0xBB, 0xBB, 0x70, 0xEF, 0x3D, 0xE4, 0x2F, 0x01, 0x00}), local));
  cache.update(local);
  CHECK(cache.get(24)->has_device);
  CHECK(!cache.get(24)->has_model);
  CHECK_EQ(cache.get(24)->device_id[2], 0x2F);

  // Ein RM ohne gelernte ID liefert 000000.
  CHECK(parse_local(make_frame({0xD4, 0x06, 0x19, 0x86, 0xBB, 0xBB, 0x70, 0xEF, 0x00, 0x00, 0x00, 0x00, 0x00}), local));
  cache.update(local);
  CHECK(!cache.get(25)->has_device);

  CHECK(cache.get(64) == nullptr);
  cache.clear();
  CHECK(!cache.get(16)->has_device);
}

}  // namespace

void run_radio_tests() {
  test_catalog();
  test_parse_examples();
  test_describe_radio();
  test_duplicates();
  test_node_cache();
}
