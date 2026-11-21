// Event helpers on top of the generated schema types (shared/gen/events.gen.hpp).
// EventStamper fills the common envelope fields (seq, source, t_mono_ms) and serializes.
#pragma once

#include <atomic>
#include <string>

#include "common/timing.hpp"
#include "events.gen.hpp"

namespace brim::events {

namespace s = brim::schema;

// Bus channel names. On QNX these are name_attach() names (/dev/name/local/<name>);
// on the laptop they are Unix datagram sockets <run_dir>/<name>.sock.
inline constexpr const char* kAppServerChannel = "brim_events";   // brim/supervisor -> app_server
inline constexpr const char* kSupervisorChannel = "brim_supervisor"; // brim -> supervisor heartbeats
inline constexpr const char* kBrimCommandChannel = "brim_cmd";     // app_server -> brim commands

// Pulse codes (QNX: added to _PULSE_CODE_MINAVAIL).
inline constexpr int kPulseHeartbeat = 1;

class EventStamper {
 public:
  explicit EventStamper(std::string source) : source_(std::move(source)) {}

  // Fills seq/source/t_mono_ms on any generated event struct and returns its JSON text.
  template <class E>
  std::string stamp(E& e) {
    e.seq = ++seq_;
    e.source = source_;
    e.t_mono_ms = timing::mono_ms();
    return s::to_json(e).dump();
  }

 private:
  std::string source_;
  std::atomic<std::int64_t> seq_{0};
};

}  // namespace brim::events
