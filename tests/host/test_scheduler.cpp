#include "check.h"

#include "scheduler.h"

#include <initializer_list>
#include <vector>

using namespace esphome::wisafe2;

namespace {

struct FakeTransport : Transport {
  std::vector<std::vector<uint8_t>> sent;
  uint8_t next_id{0};
  bool accept{true};

  bool send_bytes(const uint8_t *bytes, size_t len, uint8_t &id) override {
    if (!this->accept) {
      return false;
    }
    this->sent.emplace_back(bytes, bytes + len);
    id = ++this->next_id;
    return true;
  }
};

struct Result {
  uint8_t tag;
  Outcome outcome;
  bool has_reply;
  uint8_t reply_first;
};

struct Fixture {
  FakeTransport transport;
  Scheduler scheduler{transport};
  std::vector<Result> results;
  uint32_t now{10000};

  Fixture() {
    this->scheduler.set_handler([this](const Completion &c) {
      this->results.push_back(
          Result{c.request.tag, c.outcome, c.reply != nullptr, c.reply != nullptr ? c.reply->data[0] : uint8_t(0)});
    });
  }

  void advance(uint32_t ms) {
    for (uint32_t step = 0; step < ms; step += 10) {
      this->now += 10;
      this->scheduler.service(this->now, false);
    }
  }

  void sent_ok() {
    this->scheduler.on_send_result(this->transport.next_id, true,
                                   static_cast<uint8_t>(this->transport.sent.back().size()), this->now);
  }

  void sent_failed(uint8_t sent) {
    this->scheduler.on_send_result(this->transport.next_id, false, sent, this->now);
  }

  void frame(std::initializer_list<uint8_t> bytes) {
    Frame frame;
    for (uint8_t byte : bytes) {
      frame.data[frame.len++] = byte;
    }
    this->scheduler.on_frame(frame, this->now);
  }
};

const uint8_t D1[] = {0xD1};
const uint8_t D2_PREFIX[] = {0xD2};
const uint8_t D3_03[] = {0xD3, 0x03};
const uint8_t D4_03_PREFIX[] = {0xD4, 0x03};
const uint8_t INIT[] = {0xD3, 0x19, 0x50, 0x00};
const uint8_t REMOTE_STATUS[] = {0xD3, 0x06, 0x05, 0x00};
const uint8_t REMOTE_STATUS_PREFIX[] = {0xD4, 0x06, 0x05};

void test_first_command_starts_immediately() {
  Fixture f;
  CHECK(f.scheduler.enqueue(Request::remote(1, INIT, sizeof(INIT))));
  f.scheduler.service(f.now, false);
  CHECK_EQ(f.transport.sent.size(), 1);
  CHECK(f.transport.sent[0] == (std::vector<uint8_t>{0xD3, 0x19, 0x50, 0x00, 0x7E}));
}

void test_remote_ack_only() {
  Fixture f;
  f.scheduler.enqueue(Request::remote(1, INIT, sizeof(INIT)));
  f.scheduler.service(f.now, false);
  f.sent_ok();
  f.advance(60);
  CHECK(f.results.empty());
  f.frame({0x46});
  CHECK_EQ(f.results.size(), 1);
  CHECK_EQ(f.results[0].tag, 1);
  CHECK(f.results[0].outcome == Outcome::Ok);
  CHECK(!f.scheduler.busy());
}

void test_ack_before_send_result() {
  Fixture f;
  f.scheduler.enqueue(Request::remote(1, INIT, sizeof(INIT)));
  f.scheduler.service(f.now, false);
  f.frame({0x46});
  CHECK(f.results.empty());
  f.sent_ok();
  CHECK_EQ(f.results.size(), 1);
  CHECK(f.results[0].outcome == Outcome::Ok);
}

void test_no_ack() {
  Fixture f;
  f.scheduler.enqueue(Request::remote(1, INIT, sizeof(INIT)));
  f.scheduler.service(f.now, false);
  f.sent_ok();
  f.advance(140);
  CHECK(f.results.empty());
  f.advance(20);
  CHECK_EQ(f.results.size(), 1);
  CHECK(f.results[0].outcome == Outcome::NoAck);
}

void test_reject() {
  Fixture f;
  f.scheduler.enqueue(Request::remote(1, REMOTE_STATUS, sizeof(REMOTE_STATUS), REMOTE_STATUS_PREFIX, 3));
  f.scheduler.service(f.now, false);
  f.sent_ok();
  f.frame({0x47});
  CHECK_EQ(f.results.size(), 1);
  CHECK(f.results[0].outcome == Outcome::Rejected);
  CHECK(!f.scheduler.remote_open());
}

void test_send_failed() {
  Fixture f;
  // Abbruch mitten im Befehl: keine Wiederholung, das Modul hat schon Bytes gesehen.
  f.scheduler.enqueue(Request::local(1, D3_03, 2, D4_03_PREFIX, 2));
  f.scheduler.service(f.now, false);
  f.sent_failed(1);
  CHECK_EQ(f.results.size(), 1);
  CHECK(f.results[0].outcome == Outcome::SendFailed);
  CHECK_EQ(f.transport.sent.size(), 1);

  // Ohne Meldung von Kern 1.
  f.scheduler.enqueue(Request::local(2, D1, 1, D2_PREFIX, 1));
  f.advance(600);
  CHECK_EQ(f.transport.sent.size(), 2);
  f.advance(1000);
  CHECK_EQ(f.results.size(), 2);
  CHECK(f.results[1].outcome == Outcome::SendFailed);

  // Warteschlange von Kern 1 voll.
  f.transport.accept = false;
  f.scheduler.enqueue(Request::local(3, D1, 1, D2_PREFIX, 1));
  f.advance(600);
  CHECK_EQ(f.results.size(), 3);
  CHECK(f.results[2].outcome == Outcome::SendFailed);
}

void test_retry_when_no_byte_taken() {
  Fixture f;
  f.scheduler.enqueue(Request::local(1, D1, 1, D2_PREFIX, 1));
  f.scheduler.enqueue(Request::local(2, D3_03, 2, D4_03_PREFIX, 2));
  f.scheduler.service(f.now, false);
  f.sent_failed(0);
  CHECK(f.results.empty());
  CHECK(!f.scheduler.busy());

  // Wiederholt wird derselbe Befehl, frühestens nach der Wartezeit.
  f.advance(SEND_RETRY_DELAY_MS - 10);
  CHECK_EQ(f.transport.sent.size(), 1);
  f.advance(10);
  CHECK_EQ(f.transport.sent.size(), 2);
  CHECK(f.transport.sent[1] == (std::vector<uint8_t>{0xD1, 0x7E}));
  f.sent_ok();
  f.frame({0xD2, 0x2B, 0x2E, 0x41, 0x65, 0xDF, 0xA5, 0xB8, 0x13, 0x00, 0x01, 0x10, 0x00});
  CHECK_EQ(f.results.size(), 1);
  CHECK_EQ(f.results[0].tag, 1);
  CHECK(f.results[0].outcome == Outcome::Ok);
}

void test_retry_gives_up() {
  Fixture f;
  f.scheduler.enqueue(Request::local(1, D1, 1, D2_PREFIX, 1));
  f.scheduler.service(f.now, false);
  for (int attempt = 1; attempt < SEND_ATTEMPTS; ++attempt) {
    f.sent_failed(0);
    f.advance(SEND_RETRY_DELAY_MS);
  }
  CHECK_EQ(f.transport.sent.size(), SEND_ATTEMPTS);
  CHECK(f.results.empty());
  f.sent_failed(0);
  CHECK_EQ(f.results.size(), 1);
  CHECK(f.results[0].outcome == Outcome::SendFailed);
  f.advance(2000);
  CHECK_EQ(f.transport.sent.size(), SEND_ATTEMPTS);
}

void test_host_reply_retry_keeps_priority() {
  Fixture f;
  f.scheduler.enqueue(Request::local(1, D1, 1, D2_PREFIX, 1));
  const uint8_t identity[] = {0x91, 0xA5, 0xB8, 0x14, 0x11, 0x03, 0xFF, 0x05, 0x01, 0x01};
  f.scheduler.enqueue(Request::host_reply(2, identity, sizeof(identity), false));
  f.scheduler.service(f.now, false);
  CHECK_EQ(f.transport.sent[0][0], 0x91);
  f.sent_failed(0);
  f.advance(SEND_RETRY_DELAY_MS);
  CHECK_EQ(f.transport.sent.size(), 2);
  CHECK_EQ(f.transport.sent[1][0], 0x91);
}

void test_local_reply_and_quiet_time() {
  Fixture f;
  f.scheduler.enqueue(Request::local(1, D1, 1, D2_PREFIX, 1));
  f.scheduler.enqueue(Request::local(2, D3_03, 2, D4_03_PREFIX, 2));
  f.scheduler.service(f.now, false);
  CHECK_EQ(f.transport.sent.size(), 1);
  f.sent_ok();
  f.advance(70);
  f.frame({0xD2, 0x2B, 0x2E, 0x41, 0x65, 0xDF, 0xA5, 0xB8, 0x13, 0x00, 0x01, 0x10, 0x00});
  CHECK_EQ(f.results.size(), 1);
  CHECK(f.results[0].outcome == Outcome::Ok);
  CHECK(f.results[0].has_reply);
  CHECK_EQ(f.results[0].reply_first, 0xD2);

  // D3 03 erst 500 ms nach dem letzten Rahmen.
  f.advance(490);
  CHECK_EQ(f.transport.sent.size(), 1);
  f.advance(10);
  CHECK_EQ(f.transport.sent.size(), 2);
  CHECK(f.transport.sent[1] == (std::vector<uint8_t>{0xD3, 0x03, 0x7E}));
}

void test_local_timeout_ignores_other_frames() {
  Fixture f;
  f.scheduler.enqueue(Request::local(1, D3_03, 2, D4_03_PREFIX, 2));
  f.scheduler.service(f.now, false);
  f.sent_ok();
  f.frame({0x70, 0x3D, 0xE4, 0x2F, 0x81, 0x01, 0x7C, 0x04, 0x0E, 0x03});
  f.frame({0x46});
  CHECK(f.results.empty());
  f.advance(500);
  CHECK_EQ(f.results.size(), 1);
  CHECK(f.results[0].outcome == Outcome::Timeout);
}

void test_single_open_remote() {
  Fixture f;
  f.scheduler.enqueue(Request::remote(1, REMOTE_STATUS, sizeof(REMOTE_STATUS), REMOTE_STATUS_PREFIX, 3));
  const uint8_t second[] = {0xD3, 0x06, 0x09, 0x00};
  const uint8_t second_prefix[] = {0xD4, 0x06, 0x09};
  f.scheduler.enqueue(Request::remote(2, second, sizeof(second), second_prefix, 3));
  f.scheduler.enqueue(Request::local(3, D1, 1, D2_PREFIX, 1));
  f.scheduler.service(f.now, false);
  f.sent_ok();
  f.frame({0x46});
  CHECK(f.scheduler.remote_open());
  CHECK(f.results.empty());

  // Die zweite Fernanfrage wartet; die lokale Abfrage darf vorbei.
  f.advance(600);
  CHECK_EQ(f.transport.sent.size(), 2);
  CHECK(f.transport.sent[1] == (std::vector<uint8_t>{0xD1, 0x7E}));
  f.sent_ok();
  f.frame({0xD2, 0x2B, 0x2E, 0x41, 0x65, 0xDF, 0xA5, 0xB8, 0x13, 0x00, 0x01, 0x10, 0x00});
  CHECK_EQ(f.results.size(), 1);
  CHECK_EQ(f.results[0].tag, 3);
  f.advance(3000);
  CHECK_EQ(f.transport.sent.size(), 2);

  // Antwort auf eine andere SID schließt die Anfrage nicht.
  f.frame({0xD4, 0x06, 0x18, 0x86, 0xBB, 0xBB, 0x70, 0xEF, 0x3D, 0xE4, 0x2F, 0x01, 0x00});
  CHECK(f.scheduler.remote_open());
  f.frame({0xD4, 0x06, 0x05, 0x86, 0xBB, 0xBB, 0x70, 0xEF, 0x3D, 0xE4, 0x2F, 0x01, 0x00});
  CHECK(!f.scheduler.remote_open());
  CHECK_EQ(f.results.size(), 2);
  CHECK_EQ(f.results[1].tag, 1);
  CHECK(f.results[1].has_reply);

  f.advance(500);
  CHECK_EQ(f.transport.sent.size(), 3);
  CHECK(f.transport.sent[2] == (std::vector<uint8_t>{0xD3, 0x06, 0x09, 0x00, 0x7E}));
}

void test_remote_reply_timeout() {
  Fixture f;
  f.scheduler.enqueue(Request::remote(1, REMOTE_STATUS, sizeof(REMOTE_STATUS), REMOTE_STATUS_PREFIX, 3));
  f.scheduler.service(f.now, false);
  f.sent_ok();
  f.frame({0x46});
  f.advance(REMOTE_REPLY_TIMEOUT_MS - 20);
  CHECK(f.results.empty());
  f.advance(20);
  CHECK_EQ(f.results.size(), 1);
  CHECK(f.results[0].outcome == Outcome::Timeout);
}

void test_remote_timeout_setting() {
  Fixture f;
  f.scheduler.set_remote_timeout(60000);
  CHECK_EQ(f.scheduler.remote_timeout(), 60000u);
  f.scheduler.enqueue(Request::remote(1, REMOTE_STATUS, sizeof(REMOTE_STATUS), REMOTE_STATUS_PREFIX, 3));
  f.scheduler.service(f.now, false);
  f.sent_ok();
  f.frame({0x46});
  f.advance(60000 - 20);
  CHECK(f.results.empty());
  f.advance(20);
  CHECK_EQ(f.results.size(), 1);
  CHECK(f.results[0].outcome == Outcome::Timeout);

  // Eine späte Antwort nach dem Zeitlimit beendet nichts mehr.
  f.frame({0xD4, 0x06, 0x05, 0x86, 0xBB, 0xBB, 0x70, 0xEF, 0x3D, 0xE4, 0x2F, 0x01, 0x00});
  CHECK_EQ(f.results.size(), 1);
}

void test_unreachable() {
  Fixture f;
  f.scheduler.enqueue(
      Request::remote(1, REMOTE_STATUS, sizeof(REMOTE_STATUS), REMOTE_STATUS_PREFIX, 3).to_sid(5));
  f.scheduler.service(f.now, false);
  f.sent_ok();
  f.frame({0x46});
  // D4 01 nach D3 04 und D4 01 für eine andere SID beenden die Anfrage nicht.
  f.frame({0xD4, 0x01, 0x04, 0x20, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00});
  f.frame({0xD4, 0x01, 0x00, 0x00, 0x00, 0x00, 0x02, 0x00, 0x00, 0x00, 0x00});
  CHECK(f.scheduler.remote_open());
  f.frame({0xD4, 0x01, 0x00, 0x20, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00});
  CHECK(!f.scheduler.remote_open());
  CHECK_EQ(f.results.size(), 1);
  CHECK(f.results[0].outcome == Outcome::Unreachable);
  CHECK(!f.results[0].has_reply);
}

void test_reply_from_sid() {
  Fixture f;
  const uint8_t identity_query[] = {0xD3, 0x06, 0x05, 0x01};
  const uint8_t c4[] = {0xC4};
  f.scheduler.enqueue(Request::remote(1, identity_query, sizeof(identity_query), c4, 1).from_sid(5).to_sid(5));
  f.scheduler.service(f.now, false);
  f.sent_ok();
  f.frame({0x46});
  // Kopie einer C4 auf die Anfrage eines anderen Knotens.
  f.frame({0xC4, 0xA5, 0xB8, 0x13, 0xFF, 0x11, 0x03, 0x10, 0x02});
  CHECK(f.scheduler.remote_open());
  f.frame({0xC4, 0x08, 0x2F, 0x13, 0x82, 0x11, 0x04, 0x05, 0x03});
  CHECK(!f.scheduler.remote_open());
  CHECK_EQ(f.results.size(), 1);
  CHECK(f.results[0].outcome == Outcome::Ok);
  CHECK_EQ(f.results[0].reply_first, 0xC4);
}

void test_reply_by_device() {
  Fixture f;
  const uint8_t battery_query[] = {0xC7, 0xDC, 0xF0, 0x33, 0x7C, 0x04, 0x06};
  const uint8_t c8[] = {0xC8, 0xDC, 0xF0, 0x33};
  f.scheduler.enqueue(Request::remote(1, battery_query, sizeof(battery_query), c8, sizeof(c8)));
  f.scheduler.service(f.now, false);
  f.sent_ok();
  f.frame({0x46});
  f.frame({0xC8, 0xDC, 0xF0, 0x34, 0x06, 0x8A, 0x0B, 0x00, 0x00, 0x0C, 0x0D});
  CHECK(f.scheduler.remote_open());
  f.frame({0xC8, 0xDC, 0xF0, 0x33, 0x06, 0x8A, 0x0B, 0x00, 0x00, 0x0B, 0x0D});
  CHECK(!f.scheduler.remote_open());
  CHECK(f.results[0].outcome == Outcome::Ok);
}

void test_host_reply_priority() {
  Fixture f;
  f.scheduler.enqueue(Request::local(1, D1, 1, D2_PREFIX, 1));
  f.scheduler.service(f.now, false);
  f.sent_ok();
  f.frame({0xD2, 0x2B, 0x2E, 0x41, 0x65, 0xDF, 0xA5, 0xB8, 0x13, 0x00, 0x01, 0x10, 0x00});
  f.scheduler.enqueue(Request::local(2, D3_03, 2, D4_03_PREFIX, 2));
  const uint8_t identity[] = {0x91, 0xA5, 0xB8, 0x14, 0x11, 0x03, 0xFF, 0x05, 0x01, 0x01};
  f.scheduler.enqueue(Request::host_reply(3, identity, sizeof(identity), false));

  // Hostantwort ohne Ruhezeit und vor der wartenden Abfrage.
  f.advance(10);
  CHECK_EQ(f.transport.sent.size(), 2);
  CHECK_EQ(f.transport.sent[1][0], 0x91);
  f.sent_ok();
  CHECK_EQ(f.results.size(), 2);
  CHECK_EQ(f.results[1].tag, 3);
  CHECK(f.results[1].outcome == Outcome::Ok);
}

void test_masking_on_send() {
  Fixture f;
  const uint8_t identity[] = {0x91, 0x7E, 0xB8, 0x7D, 0x11, 0x03, 0xFF, 0x05, 0x01, 0x01};
  f.scheduler.enqueue(Request::host_reply(1, identity, sizeof(identity), false));
  f.scheduler.service(f.now, false);
  CHECK(f.transport.sent[0] ==
        (std::vector<uint8_t>{0x91, 0x7D, 0x01, 0xB8, 0x7D, 0x02, 0x11, 0x03, 0xFF, 0x05, 0x01, 0x01, 0x7E}));
}

void test_waits_while_receiving() {
  Fixture f;
  const uint8_t identity[] = {0x91, 0xA5, 0xB8, 0x14, 0x11, 0x03, 0xFF, 0x05, 0x01, 0x01};
  f.scheduler.enqueue(Request::host_reply(1, identity, sizeof(identity), false));
  f.scheduler.service(f.now, true);
  CHECK(f.transport.sent.empty());
  f.scheduler.service(f.now, false);
  CHECK_EQ(f.transport.sent.size(), 1);
}

void test_queue_limits() {
  Fixture f;
  int accepted = 0;
  for (int index = 0; index < 12; ++index) {
    accepted += f.scheduler.enqueue(Request::local(1, D1, 1, D2_PREFIX, 1)) ? 1 : 0;
  }
  CHECK_EQ(accepted, 8);
  uint8_t too_long[MAX_PAYLOAD + 1] = {0x91};
  CHECK(!f.scheduler.enqueue(Request::host_reply(1, too_long, sizeof(too_long), false)));
  f.scheduler.clear();
  CHECK_EQ(f.scheduler.pending(), 0);
}

}  // namespace

void run_scheduler_tests() {
  test_first_command_starts_immediately();
  test_remote_ack_only();
  test_ack_before_send_result();
  test_no_ack();
  test_reject();
  test_send_failed();
  test_retry_when_no_byte_taken();
  test_retry_gives_up();
  test_host_reply_retry_keeps_priority();
  test_local_reply_and_quiet_time();
  test_local_timeout_ignores_other_frames();
  test_single_open_remote();
  test_remote_reply_timeout();
  test_remote_timeout_setting();
  test_unreachable();
  test_reply_from_sid();
  test_reply_by_device();
  test_host_reply_priority();
  test_masking_on_send();
  test_waits_while_receiving();
  test_queue_limits();
}
