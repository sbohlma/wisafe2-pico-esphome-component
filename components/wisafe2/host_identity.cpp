#include "host_identity.h"

#include "catalog.h"

#include <cstring>

namespace esphome {
namespace wisafe2 {
namespace {

class Writer {
 public:
  Writer(uint8_t *out, size_t cap) : out_(out), cap_(cap) {}

  void byte(uint8_t value) {
    if (this->len_ < this->cap_) {
      this->out_[this->len_] = value;
    }
    this->len_++;
  }
  void bytes(const uint8_t *values, size_t count) {
    for (size_t index = 0; index < count; ++index) {
      this->byte(values[index]);
    }
  }
  void model(uint16_t model) {
    uint8_t packed[2];
    write_model(model, packed);
    this->bytes(packed, sizeof(packed));
  }
  size_t finish() const { return this->len_ <= this->cap_ ? this->len_ : 0; }

 protected:
  uint8_t *out_;
  size_t cap_;
  size_t len_{0};
};

bool is_own_device(const RadioMessage &request, const Identity &identity) {
  return request.has_device && std::memcmp(request.device_id, identity.device_id, 3) == 0;
}

}  // namespace

size_t build_identity_reply(const Identity &identity, uint8_t *out, size_t out_cap) {
  if (!identity.valid) {
    return 0;
  }
  Writer writer(out, out_cap);
  writer.byte(MSG_IDENTITY_REPLY);
  writer.bytes(identity.device_id, 3);
  writer.model(identity.model);
  writer.byte(HOST_CLASS);
  writer.byte(0x05);
  writer.byte(0x01);
  writer.byte(0x01);
  return writer.finish();
}

size_t build_host_reply(const RadioMessage &request, const Identity &identity, uint8_t *out, size_t out_cap) {
  if (!identity.valid) {
    return 0;
  }
  Writer writer(out, out_cap);
  switch (request.type) {
    case RadioType::IdRequest:
      writer.byte(0xC4);
      writer.bytes(identity.device_id, 3);
      writer.byte(HOST_CLASS);
      writer.model(identity.model);
      break;
    case RadioType::FaultDetailsRequest:
      if (!is_own_device(request, identity)) {
        return 0;
      }
      writer.byte(0x73);
      writer.bytes(identity.device_id, 3);
      writer.model(identity.model);
      writer.byte(0x00);
      writer.byte(0x00);
      break;
    case RadioType::BatteryRequest:
      if (!is_own_device(request, identity)) {
        return 0;
      }
      writer.byte(0xC8);
      writer.bytes(identity.device_id, 3);
      // Echo von d6 der Anfrage: 06, beim WHM-1 05.
      writer.byte(request.value);
      writer.byte(static_cast<uint8_t>(HOST_BATTERY_MV & 0xFF));
      writer.byte(static_cast<uint8_t>(HOST_BATTERY_MV >> 8));
      writer.byte(0x00);
      writer.byte(0x00);
      break;
    case RadioType::FaultEventsRequest:
      if (!is_own_device(request, identity)) {
        return 0;
      }
      writer.byte(0xC6);
      writer.bytes(identity.device_id, 3);
      writer.byte(0x74);
      writer.byte(0x00);
      writer.byte(0x00);
      writer.byte(0x00);
      writer.byte(0x00);
      break;
    case RadioType::DiagnoseRequest:
      writer.byte(0xC2);
      writer.bytes(identity.device_id, 3);
      writer.byte(HOST_CLASS);
      writer.model(identity.model);
      writer.byte(0x01);
      break;
    default:
      return 0;
  }
  return writer.finish();
}

int ReplyLimiter::slot_(uint8_t code) {
  switch (code) {
    case 0x72:
      return 0;
    case 0xC1:
      return 1;
    case 0xC3:
      return 2;
    case 0xC5:
      return 3;
    case 0xC7:
      return 4;
    default:
      return -1;
  }
}

bool ReplyLimiter::allow(uint8_t code, uint32_t now_ms) {
  const int slot = slot_(code);
  if (slot < 0) {
    return true;
  }
  if (this->used_[slot] && now_ms - this->last_ms_[slot] < WINDOW_MS) {
    return false;
  }
  this->used_[slot] = true;
  this->last_ms_[slot] = now_ms;
  return true;
}

}  // namespace wisafe2
}  // namespace esphome
