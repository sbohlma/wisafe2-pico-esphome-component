#pragma once

#include "framing.h"

#include <cstddef>
#include <cstdint>
#include <functional>
#include <utility>

namespace esphome {
namespace wisafe2 {

// Unmaskiert. Maskiert mit 7E höchstens 31 Bytes, das passt in einen Befehl für Kern 1.
static constexpr size_t MAX_PAYLOAD = 15;
// Ruhe nach jedem gesendeten oder empfangenen Rahmen, bevor der nächste Befehl startet. Antworten an das
// RM (Hostantworten) warten nicht darauf.
static constexpr uint32_t QUIET_MS = 500;
// 46/47 kommt nach etwa 60 ms.
static constexpr uint32_t ACK_TIMEOUT_MS = 150;
// D2 und D4 03 kommen nach etwa 70 ms.
static constexpr uint32_t LOCAL_REPLY_TIMEOUT_MS = 500;
// Fernantworten kamen nach bis zu 50 s, D4 01 nach etwa 45 s.
static constexpr uint32_t REMOTE_REPLY_TIMEOUT_MS = 55000;
// Kern 1 meldet das Ende eines Befehls; bleibt die Meldung aus, gilt der Befehl als nicht gesendet.
static constexpr uint32_t SEND_TIMEOUT_MS = 1000;
// Nach dem Init nimmt das Modul einige Sekunden lang keine Bytes an. Ein Befehl, von dem noch kein Byte
// hinausging, wird deshalb wiederholt: bis zu 20 Versuche im Abstand von mindestens 500 ms.
static constexpr uint8_t SEND_ATTEMPTS = 20;
static constexpr uint32_t SEND_RETRY_DELAY_MS = 500;

enum class RequestKind : uint8_t {
  HostReply,  // Antwort an das RM, etwa 91 auf 41; sofort, vor allen anderen
  Local,      // lokale Abfrage des RM, etwa D1; Antwort ohne 46
  Remote,     // geht über Funk; 46 oder 47, danach gegebenenfalls eine Antwort aus dem Netz
};

enum class Outcome : uint8_t {
  Ok,
  Rejected,    // 47
  NoAck,       // weder 46 noch 47
  Timeout,      // erwartete Antwort blieb aus
  SendFailed,   // Kern 1 hat den Befehl nicht vollständig abgesetzt
  Unreachable,  // D4 01 mit der Ziel-SID in der Missing-Map
};

static constexpr uint8_t NO_SID = 0xFF;

const char *outcome_name(Outcome outcome);

struct Request {
  RequestKind kind{RequestKind::Local};
  uint8_t tag{0};
  uint8_t len{0};
  uint8_t payload[MAX_PAYLOAD]{};
  bool expects_ack{false};
  // Die Antwort beginnt mit diesen Bytes. 0: keine Antwort erwartet.
  uint8_t reply_len{0};
  uint8_t reply_prefix[4]{};
  // Falls gesetzt, muss die Antwort von dieser SID stammen (vorletztes Byte). Nach dem Init bekommt der Host
  // Kopien aller Hostantworten, auch C4 auf Anfragen anderer Knoten.
  uint8_t reply_sid{NO_SID};
  // Falls gesetzt, beendet D4 01 mit dieser SID die Anfrage als nicht erreichbar.
  uint8_t target_sid{NO_SID};
  uint32_t reply_timeout_ms{0};
  uint8_t send_attempts{0};

  static Request host_reply(uint8_t tag, const uint8_t *payload, size_t len, bool expects_ack);
  static Request local(uint8_t tag, const uint8_t *payload, size_t len, const uint8_t *reply_prefix,
                       size_t reply_len);
  static Request remote(uint8_t tag, const uint8_t *payload, size_t len, const uint8_t *reply_prefix = nullptr,
                        size_t reply_len = 0);

  Request &from_sid(uint8_t sid) {
    this->reply_sid = sid;
    return *this;
  }
  Request &to_sid(uint8_t sid) {
    this->target_sid = sid;
    return *this;
  }

  bool matches_reply(const Frame &frame) const;
  bool matches_unreachable(const Frame &frame) const;
};

struct Completion {
  const Request &request;
  Outcome outcome;
  // Nur bei Ok und erwarteter Antwort gesetzt.
  const Frame *reply;
};

class Transport {
 public:
  // Reiht fertig maskierte Bytes für Kern 1 ein; id ordnet die spätere Sendemeldung zu.
  virtual bool send_bytes(const uint8_t *bytes, size_t len, uint8_t &id) = 0;

 protected:
  ~Transport() = default;
};

// Sendet zu jeder Zeit höchstens einen Befehl und hält höchstens eine Fernanfrage offen. Eine zweite
// Fernanfrage vor dem Abschluss der ersten ging am Netz regelmäßig verloren.
class Scheduler {
 public:
  using Handler = std::function<void(const Completion &)>;

  explicit Scheduler(Transport &transport) : transport_(transport) {}

  void set_handler(Handler handler) { this->handler_ = std::move(handler); }
  // false, wenn die Warteschlange voll oder der Inhalt zu lang ist.
  bool enqueue(const Request &request);
  // sent: Anzahl der Bytes, die Kern 1 abgesetzt hat.
  void on_send_result(uint8_t id, bool ok, uint8_t sent, uint32_t now_ms);
  // true, wenn der Rahmen eine offene Anfrage quittiert oder beantwortet.
  bool on_frame(const Frame &frame, uint32_t now_ms);
  // receiving: ein Rahmen vom RM ist gerade angefangen; dann startet kein Befehl.
  void service(uint32_t now_ms, bool receiving);
  // Verwirft alle wartenden und offenen Anfragen ohne Meldung.
  void clear();

  bool busy() const { return this->phase_ != Phase::Idle; }
  bool remote_open() const { return this->remote_open_; }
  size_t pending() const { return this->host_count_ + this->queue_count_; }

 protected:
  enum class Phase : uint8_t { Idle, Sending, AwaitAck, AwaitReply };
  enum class EarlyAck : uint8_t { None, Ack, Reject };

  static constexpr size_t HOST_QUEUE = 4;
  static constexpr size_t QUEUE = 8;

  void start_(const Request &request, uint32_t now_ms);
  bool requeue_front_(const Request &request);
  void acknowledged_(uint32_t now_ms);
  void finish_active_(Outcome outcome, const Frame *reply);
  void finish_remote_(Outcome outcome, const Frame *reply);

  Transport &transport_;
  Handler handler_;

  Request host_queue_[HOST_QUEUE]{};
  size_t host_count_{0};
  Request queue_[QUEUE]{};
  size_t queue_count_{0};

  Phase phase_{Phase::Idle};
  Request active_{};
  uint8_t active_id_{0};
  uint32_t phase_since_ms_{0};
  EarlyAck early_ack_{EarlyAck::None};

  bool remote_open_{false};
  Request remote_{};
  uint32_t remote_since_ms_{0};

  bool any_activity_{false};
  uint32_t last_activity_ms_{0};

  bool hold_{false};
  uint32_t hold_until_ms_{0};
};

}  // namespace wisafe2
}  // namespace esphome
