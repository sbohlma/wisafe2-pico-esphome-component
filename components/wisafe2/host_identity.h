#pragma once

#include "messages.h"

#include <cstddef>
#include <cstdint>

namespace esphome {
namespace wisafe2 {

// Klasse FF wie die Arduino-Firmware beim Pairing; das Netz behandelt die Brücke damit wie ein Blitzlicht.
static constexpr uint8_t HOST_CLASS = 0xFF;
static constexpr uint16_t HOST_BATTERY_MV = 3000;
static constexpr size_t HOST_REPLY_MAX = 10;

struct Identity {
  bool valid{false};
  uint8_t device_id[3]{};  // Paket-Reihenfolge
  uint16_t model{0};
};

// 91 dev model FF 05 01 01, Antwort auf 41.
size_t build_identity_reply(const Identity &identity, uint8_t *out, size_t out_cap);

// Antwort auf eine Anfrage an den Host mit festen Werten (keine Fehler, 3000 mV, keine Ereignisse, Diagnose
// bestanden). 0, wenn keine Antwort fällig ist: 72, C5 und C7 werden an jeden Host geflutet und nur für die
// eigene Geräte-ID beantwortet. SID und Sequenznummer hängt das RM an.
size_t build_host_reply(const RadioMessage &request, const Identity &identity, uint8_t *out, size_t out_cap);

// Beantwortet jede Art von Anfrage höchstens einmal je 60 s.
class ReplyLimiter {
 public:
  static constexpr uint32_t WINDOW_MS = 60000;

  bool allow(uint8_t code, uint32_t now_ms);

 protected:
  static constexpr size_t CODES = 5;
  static int slot_(uint8_t code);

  bool used_[CODES]{};
  uint32_t last_ms_[CODES]{};
};

}  // namespace wisafe2
}  // namespace esphome
