#include "check.h"

#include "frame_analyzer.h"
#include "messages.h"

#include <initializer_list>

using namespace esphome::wisafe2;

namespace {

Frame make_frame(std::initializer_list<uint8_t> bytes) {
  Frame frame;
  for (uint8_t byte : bytes) {
    frame.data[frame.len++] = byte;
  }
  return frame;
}

void test_single_byte_replies() {
  LocalMessage msg;
  CHECK(parse_local(make_frame({0x46}), msg));
  CHECK(msg.type == LocalType::Ack);
  CHECK(parse_local(make_frame({0x47}), msg));
  CHECK(msg.type == LocalType::Reject);
  CHECK(parse_local(make_frame({0x41}), msg));
  CHECK(msg.type == LocalType::IdentityRequest);
  CHECK(!parse_local(make_frame({0x48}), msg));
}

void test_rm_status() {
  // An der Bridge eingelernt, 41 mit 91 A5 B8 13 beantwortet.
  LocalMessage msg;
  CHECK(parse_local(make_frame({0xD2, 0x2B, 0x2E, 0x41, 0x65, 0xDF, 0xA5, 0xB8, 0x13, 0x00, 0x01, 0x10, 0x00}), msg));
  CHECK(msg.type == LocalType::RmStatus);
  CHECK_EQ(msg.rm.bvnl, 0x2B);
  CHECK_EQ(msg.rm.rssi, 0x65);
  CHECK_EQ(msg.rm.firmware, 0xDF);
  CHECK_EQ(msg.rm.device_id[0], 0xA5);
  CHECK_EQ(msg.rm.device_id[2], 0x13);
  CHECK_EQ(msg.rm.critical, 0x00);
  CHECK_EQ(msg.rm.radio_faults, 0x01);
  CHECK_EQ(msg.sid, 0x10);

  // Ungekoppelt.
  CHECK(parse_local(make_frame({0xD2, 0x60, 0x7E, 0x7E, 0x00, 0xEF, 0x00, 0x00, 0x00, 0x00, 0x00, 0x40, 0x40}), msg));
  CHECK_EQ(msg.sid, SID_UNPAIRED);
  CHECK_EQ(msg.rm_status_d12, 0x40);

  CHECK(!parse_local(make_frame({0xD2, 0x60, 0x7E}), msg));
}

void test_rm_reports() {
  LocalMessage msg;
  // Ziel SID 25 nicht erreichbar.
  CHECK(parse_local(make_frame({0xD4, 0x01, 0x00, 0x00, 0x00, 0x00, 0x02, 0x00, 0x00, 0x00, 0x00}), msg));
  CHECK(msg.type == LocalType::MissingMap);
  CHECK_EQ(msg.reason, 0x00);
  CHECK(msg.map == (1ull << 25));

  CHECK(parse_local(make_frame({0xD4, 0x03, 0x21, 0x40, 0x00, 0x00, 0x00, 0x00, 0x00, 0x80}), msg));
  CHECK(msg.type == LocalType::LocalSidMap);
  CHECK(msg.map == ((1ull << 0) | (1ull << 5) | (1ull << 14) | (1ull << 63)));
  CHECK_EQ(count_sids(msg.map), 4);

  CHECK(parse_local(make_frame({0xD4, 0x04, 0x01, 0, 0, 0, 0, 0, 0, 0}), msg));
  CHECK(msg.type == LocalType::NewNetworkMap);

  // SID 24, 2,5 V, RM-Batterie schwach.
  CHECK(parse_local(make_frame({0xD4, 0x06, 0x18, 0x86, 0xBB, 0xBB, 0x70, 0xEF, 0x3D, 0xE4, 0x2F, 0x01, 0x00}), msg));
  CHECK(msg.type == LocalType::RemoteStatus);
  CHECK_EQ(msg.sid, 24);
  CHECK_EQ(msg.rm.rssi, 0x70);
  CHECK_EQ(msg.rm.critical, 0x01);
  CHECK_EQ(rm_battery_mv(msg.rm.bvnl), 2502);

  // Präsenzmeldung von SID 18, Gerät 354D5F.
  CHECK(parse_local(make_frame({0xD4, 0x09, 0x12, 0x5F, 0x4D, 0x35}), msg));
  CHECK(msg.type == LocalType::PresenceHeard);
  CHECK_EQ(msg.sid, 18);
  char id[8];
  format_device_id(msg.device_id, id, sizeof(id));
  CHECK(std::strcmp(id, "354D5F") == 0);

  // Am eigenen Modul beobachtet, Untertyp unbekannt.
  CHECK(!parse_local(make_frame({0xD4, 0x10, 0xBF, 0x7F, 0x0E, 0x05, 0x00, 0x00, 0x00, 0x00, 0x14}), msg));
}

void test_battery() {
  CHECK_EQ(rm_battery_mv(0x85), 2508);
  CHECK_EQ(rm_battery_mv(0x00), 3679);
}

void test_describe() {
  char text[256];
  describe_frame(make_frame({0x46}), text, sizeof(text));
  CHECK_STR_CONTAINS(text, "Quittung 46");

  describe_frame(make_frame({0x41}), text, sizeof(text));
  CHECK_STR_CONTAINS(text, "Identitätsanfrage 41");

  describe_frame(make_frame({0xD2, 0x85, 0xBB, 0xBB, 0x66, 0xEF, 0x3D, 0xE4, 0x2F, 0x01, 0x00, 0x18, 0x00}), text,
                 sizeof(text));
  CHECK_STR_CONTAINS(text, "RM-Status: SID 24");
  CHECK_STR_CONTAINS(text, "2508 mV");
  CHECK_STR_CONTAINS(text, "ID 2FE43D");

  describe_frame(make_frame({0xD2, 0x2D, 0x32, 0x41, 0x00, 0xDF, 0x00, 0x00, 0x00, 0x00, 0x01, 0x40, 0x40}), text,
                 sizeof(text));
  CHECK_STR_CONTAINS(text, "ungekoppelt");

  describe_frame(make_frame({0xD4, 0x03, 0x21, 0x40, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00}), text, sizeof(text));
  CHECK_STR_CONTAINS(text, "3 Knoten (0, 5, 14)");

  describe_frame(make_frame({0xD4, 0x10, 0xBF, 0x7F, 0x0E, 0x05, 0x00, 0x00, 0x00, 0x00, 0x14}), text, sizeof(text));
  CHECK_STR_CONTAINS(text, "Roh: D4 10 BF 7F");

  // Testtaste SID 14, Rauch, OK, ST-630-DE, seq 3.
  describe_frame(make_frame({0x70, 0x3D, 0xE4, 0x2F, 0x81, 0x01, 0x7C, 0x04, 0x0E, 0x03}), text, sizeof(text));
  CHECK_STR_CONTAINS(text, "Test Gerät 2FE43D");
  CHECK_STR_CONTAINS(text, "Rauch, bestanden, SID 14, Folge 3");

  Frame partial = make_frame({0xD2, 0x20});
  partial.status = FrameStatus::Timeout;
  describe_frame(partial, text, sizeof(text));
  CHECK_STR_CONTAINS(text, "Roh unvollständig: D2 20");

  // Volle SID-Map mit 64 Knoten passt in die Zeile.
  describe_frame(make_frame({0xD4, 0x03, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF}), text, sizeof(text));
  CHECK_STR_CONTAINS(text, "64 Knoten");
  CHECK(std::strlen(text) < sizeof(text));
}

}  // namespace

void run_message_tests() {
  test_single_byte_replies();
  test_rm_status();
  test_rm_reports();
  test_battery();
  test_describe();
}
