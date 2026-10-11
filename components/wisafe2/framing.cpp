#include "framing.h"

#include <cstring>

namespace esphome {
namespace wisafe2 {

void FrameDecoder::reset() {
  this->len_ = 0;
  this->escape_ = false;
  this->bad_escape_ = false;
  this->discarding_ = false;
}

void FrameDecoder::finish_(FrameStatus status, Frame &out) {
  out.status = status;
  out.len = this->len_;
  std::memcpy(out.data, this->data_, this->len_);
  this->len_ = 0;
  this->escape_ = false;
  this->bad_escape_ = false;
}

bool FrameDecoder::push_(uint8_t byte, Frame &out) {
  if (this->len_ >= MAX_FRAME_LEN) {
    this->finish_(FrameStatus::Overflow, out);
    this->discarding_ = true;
    return true;
  }
  this->data_[this->len_++] = byte;
  return false;
}

bool FrameDecoder::feed(uint8_t byte, uint32_t now_ms, Frame &out) {
  this->last_byte_ms_ = now_ms;
  if (this->discarding_) {
    if (byte == FRAME_END) {
      this->discarding_ = false;
    }
    return false;
  }
  if (byte == FRAME_END) {
    if (!this->open()) {
      return false;
    }
    if (this->escape_) {
      this->bad_escape_ = true;
      this->escape_ = false;
      if (this->push_(FRAME_ESCAPE, out)) {
        return true;
      }
    }
    this->finish_(this->bad_escape_ ? FrameStatus::BadEscape : FrameStatus::Complete, out);
    return true;
  }
  // Füllbytes vor einem Rahmen; innerhalb eines Rahmens ist 00 ein gültiges Datenbyte.
  if (byte == 0x00 && !this->open()) {
    return false;
  }
  if (this->escape_) {
    this->escape_ = false;
    if (byte == 0x01) {
      return this->push_(FRAME_END, out);
    }
    if (byte == 0x02) {
      return this->push_(FRAME_ESCAPE, out);
    }
    this->bad_escape_ = true;
    if (this->push_(FRAME_ESCAPE, out)) {
      return true;
    }
    return this->push_(byte, out);
  }
  if (byte == FRAME_ESCAPE) {
    this->escape_ = true;
    return false;
  }
  return this->push_(byte, out);
}

bool FrameDecoder::poll_timeout(uint32_t now_ms, Frame &out) {
  if (now_ms - this->last_byte_ms_ < FRAME_TIMEOUT_MS) {
    return false;
  }
  if (this->discarding_) {
    this->discarding_ = false;
    return false;
  }
  if (!this->open()) {
    return false;
  }
  if (this->escape_ && this->len_ < MAX_FRAME_LEN) {
    this->data_[this->len_++] = FRAME_ESCAPE;
  }
  this->finish_(FrameStatus::Timeout, out);
  return true;
}

size_t encode_frame(const uint8_t *payload, size_t len, uint8_t *out, size_t out_cap) {
  size_t used = 0;
  for (size_t index = 0; index < len; ++index) {
    const uint8_t byte = payload[index];
    const bool masked = byte == FRAME_END || byte == FRAME_ESCAPE;
    if (used + (masked ? 2 : 1) > out_cap) {
      return 0;
    }
    if (masked) {
      out[used++] = FRAME_ESCAPE;
      out[used++] = byte == FRAME_END ? 0x01 : 0x02;
    } else {
      out[used++] = byte;
    }
  }
  if (used + 1 > out_cap) {
    return 0;
  }
  out[used++] = FRAME_END;
  return used;
}

}  // namespace wisafe2
}  // namespace esphome
