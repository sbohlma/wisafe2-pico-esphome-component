#include "scheduler.h"

#include <cstring>

namespace esphome {
namespace wisafe2 {
namespace {

constexpr size_t MAX_ENCODED = 2 * MAX_PAYLOAD + 1;

Request make_request(RequestKind kind, uint8_t tag, const uint8_t *payload, size_t len) {
  Request request{};
  request.kind = kind;
  request.tag = tag;
  if (payload != nullptr && len <= MAX_PAYLOAD) {
    request.len = static_cast<uint8_t>(len);
    std::memcpy(request.payload, payload, len);
  }
  return request;
}

void set_reply(Request &request, const uint8_t *prefix, size_t len, uint32_t timeout_ms) {
  if (prefix == nullptr || len == 0 || len > sizeof(request.reply_prefix)) {
    return;
  }
  request.reply_len = static_cast<uint8_t>(len);
  std::memcpy(request.reply_prefix, prefix, len);
  request.reply_timeout_ms = timeout_ms;
}

}  // namespace

const char *outcome_name(Outcome outcome) {
  switch (outcome) {
    case Outcome::Ok:
      return "ok";
    case Outcome::Rejected:
      return "abgelehnt (47)";
    case Outcome::NoAck:
      return "keine Quittung";
    case Outcome::Timeout:
      return "keine Antwort";
    case Outcome::SendFailed:
      return "nicht gesendet";
    case Outcome::Unreachable:
      return "nicht erreichbar (D4 01)";
  }
  return "?";
}

Request Request::host_reply(uint8_t tag, const uint8_t *payload, size_t len, bool expects_ack) {
  Request request = make_request(RequestKind::HostReply, tag, payload, len);
  request.expects_ack = expects_ack;
  return request;
}

Request Request::local(uint8_t tag, const uint8_t *payload, size_t len, const uint8_t *reply_prefix,
                       size_t reply_len) {
  Request request = make_request(RequestKind::Local, tag, payload, len);
  set_reply(request, reply_prefix, reply_len, LOCAL_REPLY_TIMEOUT_MS);
  return request;
}

Request Request::remote(uint8_t tag, const uint8_t *payload, size_t len, const uint8_t *reply_prefix,
                        size_t reply_len) {
  Request request = make_request(RequestKind::Remote, tag, payload, len);
  request.expects_ack = true;
  set_reply(request, reply_prefix, reply_len, REMOTE_REPLY_TIMEOUT_MS);
  return request;
}

bool Request::matches_reply(const Frame &frame) const {
  if (this->reply_len == 0 || frame.status != FrameStatus::Complete || frame.len < this->reply_len) {
    return false;
  }
  if (std::memcmp(frame.data, this->reply_prefix, this->reply_len) != 0) {
    return false;
  }
  return this->reply_sid == NO_SID || (frame.len >= 2 && frame.data[frame.len - 2] == this->reply_sid);
}

bool Request::matches_unreachable(const Frame &frame) const {
  // D4 01 00 map(8): Ziel nach D3 06 nicht erreichbar.
  if (this->target_sid > 63 || frame.status != FrameStatus::Complete || frame.len < 11 || frame.data[0] != 0xD4 ||
      frame.data[1] != 0x01 || frame.data[2] != 0x00) {
    return false;
  }
  return (frame.data[3 + this->target_sid / 8] & (1u << (this->target_sid % 8))) != 0;
}

bool Scheduler::enqueue(const Request &request) {
  if (request.len == 0) {
    return false;
  }
  if (request.kind == RequestKind::HostReply) {
    if (this->host_count_ >= HOST_QUEUE) {
      return false;
    }
    this->host_queue_[this->host_count_++] = request;
    return true;
  }
  if (this->queue_count_ >= QUEUE) {
    return false;
  }
  this->queue_[this->queue_count_++] = request;
  return true;
}

void Scheduler::clear() {
  this->host_count_ = 0;
  this->queue_count_ = 0;
  this->phase_ = Phase::Idle;
  this->remote_open_ = false;
  this->hold_ = false;
}

bool Scheduler::requeue_front_(const Request &request) {
  const bool host = request.kind == RequestKind::HostReply;
  Request *queue = host ? this->host_queue_ : this->queue_;
  size_t &count = host ? this->host_count_ : this->queue_count_;
  if (count >= (host ? HOST_QUEUE : QUEUE)) {
    return false;
  }
  for (size_t index = count; index > 0; --index) {
    queue[index] = queue[index - 1];
  }
  queue[0] = request;
  count++;
  return true;
}

void Scheduler::start_(const Request &request, uint32_t now_ms) {
  this->active_ = request;
  this->active_.send_attempts++;
  this->early_ack_ = EarlyAck::None;
  uint8_t encoded[MAX_ENCODED];
  const size_t len = encode_frame(request.payload, request.len, encoded, sizeof(encoded));
  uint8_t id = 0;
  if (len == 0 || !this->transport_.send_bytes(encoded, len, id)) {
    this->finish_active_(Outcome::SendFailed, nullptr);
    return;
  }
  this->active_id_ = id;
  this->phase_ = Phase::Sending;
  this->phase_since_ms_ = now_ms;
}

void Scheduler::acknowledged_(uint32_t now_ms) {
  if (this->active_.kind == RequestKind::Remote) {
    this->remote_sent_++;
  }
  if (this->active_.reply_len == 0) {
    this->finish_active_(Outcome::Ok, nullptr);
    return;
  }
  this->remote_ = this->active_;
  this->remote_.reply_timeout_ms = this->remote_timeout_ms_;
  this->remote_open_ = true;
  this->remote_since_ms_ = now_ms;
  this->phase_ = Phase::Idle;
}

void Scheduler::finish_active_(Outcome outcome, const Frame *reply) {
  const Request request = this->active_;
  this->phase_ = Phase::Idle;
  if (this->handler_) {
    this->handler_(Completion{request, outcome, reply});
  }
}

void Scheduler::finish_remote_(Outcome outcome, const Frame *reply) {
  const Request request = this->remote_;
  this->remote_open_ = false;
  if (this->handler_) {
    this->handler_(Completion{request, outcome, reply});
  }
}

void Scheduler::on_send_result(uint8_t id, bool ok, uint8_t sent, uint32_t now_ms) {
  if (this->phase_ != Phase::Sending || id != this->active_id_) {
    return;
  }
  this->any_activity_ = true;
  this->last_activity_ms_ = now_ms;
  if (!ok) {
    // Ohne abgesetztes Byte hat das Modul nichts gesehen, eine Wiederholung ist unbedenklich.
    if (sent == 0 && this->active_.send_attempts < SEND_ATTEMPTS && this->early_ack_ == EarlyAck::None &&
        this->requeue_front_(this->active_)) {
      this->phase_ = Phase::Idle;
      this->hold_ = true;
      this->hold_until_ms_ = now_ms + SEND_RETRY_DELAY_MS;
      return;
    }
    this->finish_active_(Outcome::SendFailed, nullptr);
    return;
  }
  this->phase_since_ms_ = now_ms;
  if (this->active_.expects_ack) {
    if (this->early_ack_ == EarlyAck::Ack) {
      this->acknowledged_(now_ms);
    } else if (this->early_ack_ == EarlyAck::Reject) {
      this->finish_active_(Outcome::Rejected, nullptr);
    } else {
      this->phase_ = Phase::AwaitAck;
    }
    return;
  }
  if (this->active_.reply_len != 0) {
    this->phase_ = Phase::AwaitReply;
    return;
  }
  this->finish_active_(Outcome::Ok, nullptr);
}

bool Scheduler::on_frame(const Frame &frame, uint32_t now_ms) {
  this->any_activity_ = true;
  this->last_activity_ms_ = now_ms;
  if (frame.status != FrameStatus::Complete) {
    return false;
  }
  const bool ack = frame.len == 1 && frame.data[0] == 0x46;
  const bool reject = frame.len == 1 && frame.data[0] == 0x47;
  if (ack || reject) {
    if (this->phase_ == Phase::Sending && this->active_.expects_ack && this->early_ack_ == EarlyAck::None) {
      this->early_ack_ = ack ? EarlyAck::Ack : EarlyAck::Reject;
      return true;
    }
    if (this->phase_ == Phase::AwaitAck) {
      if (ack) {
        this->acknowledged_(now_ms);
      } else {
        this->finish_active_(Outcome::Rejected, nullptr);
      }
      return true;
    }
    return false;
  }
  if (this->phase_ == Phase::AwaitReply && this->active_.matches_reply(frame)) {
    this->finish_active_(Outcome::Ok, &frame);
    return true;
  }
  if (this->remote_open_ && this->remote_.matches_reply(frame)) {
    this->finish_remote_(Outcome::Ok, &frame);
    return true;
  }
  if (this->remote_open_ && this->remote_.matches_unreachable(frame)) {
    this->finish_remote_(Outcome::Unreachable, nullptr);
    return true;
  }
  return false;
}

void Scheduler::service(uint32_t now_ms, bool receiving) {
  const uint32_t elapsed = now_ms - this->phase_since_ms_;
  switch (this->phase_) {
    case Phase::Sending:
      if (elapsed >= SEND_TIMEOUT_MS) {
        this->finish_active_(Outcome::SendFailed, nullptr);
      }
      break;
    case Phase::AwaitAck:
      if (elapsed >= ACK_TIMEOUT_MS) {
        this->finish_active_(Outcome::NoAck, nullptr);
      }
      break;
    case Phase::AwaitReply:
      if (elapsed >= this->active_.reply_timeout_ms) {
        this->finish_active_(Outcome::Timeout, nullptr);
      }
      break;
    case Phase::Idle:
      break;
  }
  if (this->remote_open_ && now_ms - this->remote_since_ms_ >= this->remote_.reply_timeout_ms) {
    this->finish_remote_(Outcome::Timeout, nullptr);
  }

  if (this->phase_ != Phase::Idle || receiving) {
    return;
  }
  if (this->hold_) {
    if (static_cast<int32_t>(now_ms - this->hold_until_ms_) < 0) {
      return;
    }
    this->hold_ = false;
  }
  if (this->host_count_ != 0) {
    const Request request = this->host_queue_[0];
    for (size_t index = 1; index < this->host_count_; ++index) {
      this->host_queue_[index - 1] = this->host_queue_[index];
    }
    this->host_count_--;
    this->start_(request, now_ms);
    return;
  }
  if (this->any_activity_ && now_ms - this->last_activity_ms_ < QUIET_MS) {
    return;
  }
  for (size_t index = 0; index < this->queue_count_; ++index) {
    if (this->queue_[index].kind == RequestKind::Remote && this->remote_open_) {
      continue;
    }
    const Request request = this->queue_[index];
    for (size_t next = index + 1; next < this->queue_count_; ++next) {
      this->queue_[next - 1] = this->queue_[next];
    }
    this->queue_count_--;
    this->start_(request, now_ms);
    return;
  }
}

bool module_silent(RequestKind kind, Outcome outcome) {
  switch (outcome) {
    case Outcome::NoAck:
    case Outcome::SendFailed:
      return true;
    case Outcome::Timeout:
      return kind == RequestKind::Local;
    default:
      return false;
  }
}

bool ModuleWatchdog::on_outcome(RequestKind kind, Outcome outcome) {
  const bool was = this->stalled();
  if (!module_silent(kind, outcome)) {
    this->misses_ = 0;
  } else if (this->misses_ < 255) {
    this->misses_++;
  }
  return was != this->stalled();
}

}  // namespace wisafe2
}  // namespace esphome
