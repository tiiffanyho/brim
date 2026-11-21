// The speak-queue contract. Decides WHAT reaches the phone and WHEN. The Pi plays nothing: speech
// comes from the phone app, so the queue's output is short text events (cues) published to
// app_server in priority order. The phone's screen reader speaks them.
//
// Rules (unit-tested in tests/test_speak_queue.cpp):
//   1. Priority: Safety (safety_alert, fault) > Nav (clear_path; traffic_light_seen in the
//      feasibility test) > Info (person_seen, obstacle_seen). FIFO within a priority. A direction
//      is never sent before the warning that made it necessary.
//   2. Safety cues are never dropped for age and never rate-limited.
//   3. Nav and Info cues older than info_max_age_ms (3 s) are dropped, never sent late: a stale
//      "step right" is worse than none.
//   4. Info: at most one per info_min_interval_ms (10 s), counted from the last one actually sent.
//      Nav is not rate-limited (it is already held and confirmed by src/nav).
//   5. Bounded: when full, the lowest-priority oldest cue is evicted; safety is never evicted by a
//      lower priority cue.
// No microphone: there is no "someone is talking" gate on the Pi. The phone waits for its own
// silence before reading info cues (polite live region).
// The queue is pure logic with an injected clock (now_ms), so it is deterministic in tests.
#pragma once

#include <cstddef>
#include <cstdint>
#include <deque>
#include <mutex>
#include <optional>
#include <string>
#include <variant>

#include "events.gen.hpp"

namespace brim::audio {

enum class Priority { Safety = 0, Nav = 1, Info = 2 };
const char* to_string(Priority p);

// The events that carry something for the phone to say.
using CueEvent = std::variant<schema::SafetyAlertEvent, schema::FaultEvent, schema::ClearPathEvent,
                              schema::PersonSeenEvent, schema::ObstacleSeenEvent, schema::TrafficLightSeenEvent>;

struct SpeechItem {
  Priority priority = Priority::Info;
  std::string text;              // what the phone will say (for the log)
  std::int64_t created_ms = 0;   // monotonic ms when the triggering event happened
  std::optional<CueEvent> event; // published (stamped at send time) when the cue is popped
};

struct SpeakQueueConfig {
  std::int64_t info_max_age_ms = 3000;
  std::int64_t info_min_interval_ms = 10000;
  std::size_t capacity = 32;
};

class SpeakQueue {
 public:
  explicit SpeakQueue(SpeakQueueConfig cfg = {}) : cfg_(cfg) {}

  // Returns false if the cue was rejected (queue full of higher-priority cues).
  bool push(SpeechItem item);

  // Next cue to send now, or nullopt if nothing is eligible.
  std::optional<SpeechItem> pop_next(std::int64_t now_ms);

  std::size_t size() const;
  std::uint64_t dropped_stale() const { return dropped_stale_; }
  std::uint64_t evicted() const { return evicted_; }
  void set_config(SpeakQueueConfig cfg);

 private:
  void drop_stale(std::int64_t now_ms);

  SpeakQueueConfig cfg_;
  mutable std::mutex mu_;
  std::deque<SpeechItem> q_[3];
  std::optional<std::int64_t> last_info_sent_ms_;
  std::uint64_t dropped_stale_ = 0;
  std::uint64_t evicted_ = 0;
};

}  // namespace brim::audio
