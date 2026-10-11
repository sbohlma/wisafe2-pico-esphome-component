#include "check.h"

#include "framing.h"

#include <vector>

using namespace esphome::wisafe2;

namespace {

std::vector<Frame> feed_all(FrameDecoder &decoder, const std::vector<uint8_t> &bytes, uint32_t now = 0) {
  std::vector<Frame> frames;
  Frame frame;
  for (uint8_t byte : bytes) {
    if (decoder.feed(byte, now, frame)) {
      frames.push_back(frame);
    }
  }
  return frames;
}

bool same(const Frame &frame, const std::vector<uint8_t> &expected) {
  return frame.len == expected.size() && std::memcmp(frame.data, expected.data(), expected.size()) == 0;
}

void test_simple_frame() {
  FrameDecoder decoder;
  auto frames = feed_all(decoder, {0x46, 0x7E});
  CHECK_EQ(frames.size(), 1);
  CHECK(frames[0].status == FrameStatus::Complete);
  CHECK(same(frames[0], {0x46}));
  CHECK(!decoder.open());
}

void test_leading_fill_bytes() {
  FrameDecoder decoder;
  auto frames = feed_all(decoder, {0x00, 0x00, 0x00, 0xD4, 0x03, 0x00, 0x00, 0x7E});
  CHECK_EQ(frames.size(), 1);
  // 00 innerhalb des Rahmens bleibt erhalten.
  CHECK(same(frames[0], {0xD4, 0x03, 0x00, 0x00}));
}

void test_demask() {
  FrameDecoder decoder;
  // Anfrage mit zusätzlichem 7E vor SID und seq: 72 dev model 00 7E 0F 04.
  auto frames = feed_all(decoder, {0x72, 0xDC, 0xF0, 0x33, 0x7C, 0x04, 0x00, 0x7D, 0x01, 0x0F, 0x04, 0x7E});
  CHECK_EQ(frames.size(), 1);
  CHECK(frames[0].status == FrameStatus::Complete);
  CHECK(same(frames[0], {0x72, 0xDC, 0xF0, 0x33, 0x7C, 0x04, 0x00, 0x7E, 0x0F, 0x04}));

  frames = feed_all(decoder, {0x11, 0x7D, 0x02, 0x22, 0x7E});
  CHECK_EQ(frames.size(), 1);
  CHECK(same(frames[0], {0x11, 0x7D, 0x22}));
}

void test_bad_escape() {
  FrameDecoder decoder;
  auto frames = feed_all(decoder, {0x11, 0x7D, 0x05, 0x22, 0x7E});
  CHECK_EQ(frames.size(), 1);
  CHECK(frames[0].status == FrameStatus::BadEscape);
  CHECK(same(frames[0], {0x11, 0x7D, 0x05, 0x22}));

  frames = feed_all(decoder, {0x11, 0x7D, 0x7E});
  CHECK_EQ(frames.size(), 1);
  CHECK(frames[0].status == FrameStatus::BadEscape);

  // Danach geht es normal weiter.
  frames = feed_all(decoder, {0x46, 0x7E});
  CHECK_EQ(frames.size(), 1);
  CHECK(frames[0].status == FrameStatus::Complete);
}

void test_timeout() {
  FrameDecoder decoder;
  Frame frame;
  CHECK(!decoder.feed(0xD2, 1000, frame));
  CHECK(!decoder.feed(0x20, 1010, frame));
  CHECK(decoder.open());
  CHECK(!decoder.poll_timeout(1100, frame));
  CHECK(decoder.poll_timeout(1110, frame));
  CHECK(frame.status == FrameStatus::Timeout);
  CHECK(same(frame, {0xD2, 0x20}));
  CHECK(!decoder.open());
  CHECK(!decoder.poll_timeout(5000, frame));
}

void test_overflow() {
  FrameDecoder decoder;
  std::vector<uint8_t> bytes(MAX_FRAME_LEN + 5, 0x11);
  bytes.push_back(0x7E);
  bytes.push_back(0x46);
  bytes.push_back(0x7E);
  auto frames = feed_all(decoder, bytes);
  CHECK_EQ(frames.size(), 2);
  CHECK(frames[0].status == FrameStatus::Overflow);
  CHECK_EQ(frames[0].len, MAX_FRAME_LEN);
  // Der Rest des zu langen Rahmens wird bis 7E verworfen.
  CHECK(frames[1].status == FrameStatus::Complete);
  CHECK(same(frames[1], {0x46}));
}

void test_empty_frames_ignored() {
  FrameDecoder decoder;
  auto frames = feed_all(decoder, {0x7E, 0x7E, 0x00, 0x7E});
  CHECK_EQ(frames.size(), 0);
}

void test_encode() {
  uint8_t out[32];
  const uint8_t init[] = {0xD3, 0x19, 0x50, 0x00};
  CHECK_EQ(encode_frame(init, sizeof(init), out, sizeof(out)), 5);
  CHECK(std::memcmp(out, "\xD3\x19\x50\x00\x7E", 5) == 0);

  const uint8_t masked[] = {0x91, 0x7E, 0x7D, 0x13};
  CHECK_EQ(encode_frame(masked, sizeof(masked), out, sizeof(out)), 7);
  CHECK(std::memcmp(out, "\x91\x7D\x01\x7D\x02\x13\x7E", 7) == 0);

  CHECK_EQ(encode_frame(masked, sizeof(masked), out, 6), 0);
}

void test_roundtrip() {
  uint8_t payload[15];
  for (int index = 0; index < 15; ++index) {
    payload[index] = static_cast<uint8_t>(0x7C + index % 4);
  }
  uint8_t encoded[31];
  const size_t len = encode_frame(payload, sizeof(payload), encoded, sizeof(encoded));
  CHECK(len > 0);
  FrameDecoder decoder;
  auto frames = feed_all(decoder, std::vector<uint8_t>(encoded, encoded + len));
  CHECK_EQ(frames.size(), 1);
  CHECK(frames[0].status == FrameStatus::Complete);
  CHECK(same(frames[0], std::vector<uint8_t>(payload, payload + sizeof(payload))));
}

}  // namespace

void run_framing_tests() {
  test_simple_frame();
  test_leading_fill_bytes();
  test_demask();
  test_bad_escape();
  test_timeout();
  test_overflow();
  test_empty_frames_ignored();
  test_encode();
  test_roundtrip();
}
