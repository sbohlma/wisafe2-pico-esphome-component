#pragma once

#include <cstddef>
#include <cstdint>

namespace esphome {
namespace wisafe2 {

static constexpr uint8_t FRAME_END = 0x7E;
static constexpr uint8_t FRAME_ESCAPE = 0x7D;
static constexpr size_t MAX_FRAME_LEN = 32;
// Die Arduino-Firmware verwirft unvollständige Rahmen nach derselben Zeit.
static constexpr uint32_t FRAME_TIMEOUT_MS = 100;

enum class FrameStatus : uint8_t { Complete, Timeout, Overflow, BadEscape };

// Inhalt eines Rahmens nach dem Demaskieren, ohne das abschließende 7E. Bei Fehlern stehen die bis dahin
// empfangenen Bytes in data, eine ungültige Maskierung roh.
struct Frame {
  FrameStatus status{FrameStatus::Complete};
  uint8_t len{0};
  uint8_t data[MAX_FRAME_LEN]{};
};

class FrameDecoder {
 public:
  // true, wenn mit diesem Byte ein Rahmen fertig oder verworfen ist; out enthält ihn dann.
  bool feed(uint8_t byte, uint32_t now_ms, Frame &out);
  // true, wenn ein offener Rahmen seit FRAME_TIMEOUT_MS kein Byte mehr bekommen hat.
  bool poll_timeout(uint32_t now_ms, Frame &out);
  // Ein Rahmen ist angefangen und noch nicht abgeschlossen.
  bool open() const { return this->len_ != 0 || this->escape_; }
  void reset();

 protected:
  bool push_(uint8_t byte, Frame &out);
  void finish_(FrameStatus status, Frame &out);

  uint8_t data_[MAX_FRAME_LEN]{};
  uint8_t len_{0};
  bool escape_{false};
  bool bad_escape_{false};
  bool discarding_{false};
  uint32_t last_byte_ms_{0};
};

// Maskiert payload und hängt 7E an. Gibt die Länge in out zurück, 0 wenn out zu klein ist.
size_t encode_frame(const uint8_t *payload, size_t len, uint8_t *out, size_t out_cap);

}  // namespace wisafe2
}  // namespace esphome
