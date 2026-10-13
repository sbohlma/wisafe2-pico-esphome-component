#include "check.h"

#include "host_identity.h"

#include <initializer_list>
#include <vector>

using namespace esphome::wisafe2;

namespace {

Frame make_frame(std::initializer_list<uint8_t> bytes) {
  Frame frame;
  for (uint8_t byte : bytes) {
    frame.data[frame.len++] = byte;
  }
  return frame;
}

Identity bridge() {
  Identity identity;
  identity.valid = true;
  identity.device_id[0] = 0xA5;
  identity.device_id[1] = 0xB8;
  identity.device_id[2] = 0x14;
  identity.model = 0x0311;
  return identity;
}

std::vector<uint8_t> reply_to(std::initializer_list<uint8_t> bytes, const Identity &identity = bridge()) {
  RadioMessage msg;
  if (!parse_radio(make_frame(bytes), msg)) {
    return {0xEE};
  }
  uint8_t out[HOST_REPLY_MAX];
  const size_t len = build_host_reply(msg, identity, out, sizeof(out));
  return std::vector<uint8_t>(out, out + len);
}

void test_identity_reply() {
  uint8_t out[HOST_REPLY_MAX];
  CHECK_EQ(build_identity_reply(bridge(), out, sizeof(out)), 10);
  CHECK(std::vector<uint8_t>(out, out + 10) ==
        (std::vector<uint8_t>{0x91, 0xA5, 0xB8, 0x14, 0x11, 0x03, 0xFF, 0x05, 0x01, 0x01}));
  CHECK_EQ(build_identity_reply(Identity{}, out, sizeof(out)), 0);
  CHECK_EQ(build_identity_reply(bridge(), out, 9), 0);
}

void test_network_test() {
  uint8_t out[HOST_REPLY_MAX];
  CHECK_EQ(build_network_test(bridge(), out, sizeof(out)), 8);
  CHECK(std::vector<uint8_t>(out, out + 8) ==
        (std::vector<uint8_t>{0x70, 0xA5, 0xB8, 0x14, 0xFF, 0x01, 0x11, 0x03}));
  CHECK_EQ(build_network_test(Identity{}, out, sizeof(out)), 0);
  CHECK_EQ(build_network_test(bridge(), out, 7), 0);
  // Mit SID und Sequenznummer des RM passt der Rahmen zum Decoder für die Testtaste eines Melders.
  RadioMessage msg;
  CHECK(parse_radio(make_frame({0x70, 0xA5, 0xB8, 0x14, 0xFF, 0x01, 0x11, 0x03, 0x18, 0x01}), msg));
  CHECK(msg.type == RadioType::Test);
  CHECK_EQ(msg.value, 0x01);
}

void test_host_replies() {
  CHECK(reply_to({0xC3, 0x00, 0x00, 0x00, 0x05, 0x02}) ==
        (std::vector<uint8_t>{0xC4, 0xA5, 0xB8, 0x14, 0xFF, 0x11, 0x03}));
  CHECK(reply_to({0x72, 0xA5, 0xB8, 0x14, 0x11, 0x03, 0x00, 0x05, 0x03}) ==
        (std::vector<uint8_t>{0x73, 0xA5, 0xB8, 0x14, 0x11, 0x03, 0x00, 0x00}));
  // 3000 mV = 0x0BB8.
  CHECK(reply_to({0xC7, 0xA5, 0xB8, 0x14, 0x11, 0x03, 0x06, 0x7E, 0x05, 0x04}) ==
        (std::vector<uint8_t>{0xC8, 0xA5, 0xB8, 0x14, 0x06, 0xB8, 0x0B, 0x00, 0x00}));
  CHECK(reply_to({0xC7, 0xA5, 0xB8, 0x14, 0x14, 0x04, 0x05, 0x05, 0x04}) ==
        (std::vector<uint8_t>{0xC8, 0xA5, 0xB8, 0x14, 0x05, 0xB8, 0x0B, 0x00, 0x00}));
  CHECK(reply_to({0xC5, 0xA5, 0xB8, 0x14, 0x11, 0x03, 0x74, 0x05, 0x05}) ==
        (std::vector<uint8_t>{0xC6, 0xA5, 0xB8, 0x14, 0x74, 0x00, 0x00, 0x00, 0x00}));
  CHECK(reply_to({0xC1, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x01, 0x05, 0x06}) ==
        (std::vector<uint8_t>{0xC2, 0xA5, 0xB8, 0x14, 0xFF, 0x11, 0x03, 0x01}));

  // Geflutete Anfragen an andere Geräte bleiben unbeantwortet.
  CHECK(reply_to({0x72, 0x3D, 0xE4, 0x2F, 0x7C, 0x04, 0x00, 0x05, 0x03}).empty());
  CHECK(reply_to({0xC7, 0x3D, 0xE4, 0x2F, 0x7C, 0x04, 0x06, 0x05, 0x04}).empty());
  CHECK(reply_to({0xC5, 0x3D, 0xE4, 0x2F, 0x7C, 0x04, 0x74, 0x05, 0x05}).empty());
  // Antworten anderer Hosts und Meldungen verlangen keine Antwort.
  CHECK(reply_to({0xC4, 0xA5, 0xB8, 0x14, 0xFF, 0x11, 0x03, 0x10, 0x02}).empty());
  CHECK(reply_to({0x70, 0x3D, 0xE4, 0x2F, 0x81, 0x01, 0x7C, 0x04, 0x0E, 0x03}).empty());
  // Ohne device_id antwortet die Brücke nicht.
  CHECK(reply_to({0xC3, 0x00, 0x00, 0x00, 0x05, 0x02}, Identity{}).empty());
}

void test_reply_limiter() {
  ReplyLimiter limiter;
  CHECK(limiter.allow(0xC7, 1000));
  CHECK(!limiter.allow(0xC7, 60999));
  CHECK(limiter.allow(0x72, 2000));
  CHECK(limiter.allow(0xC3, 2000));
  CHECK(!limiter.allow(0xC3, 3000));
  CHECK(limiter.allow(0xC7, 61000));
  // Typen ohne Begrenzung.
  CHECK(limiter.allow(0x41, 61000));
  CHECK(limiter.allow(0x41, 61001));
}

}  // namespace

void run_host_identity_tests() {
  test_identity_reply();
  test_network_test();
  test_host_replies();
  test_reply_limiter();
}
