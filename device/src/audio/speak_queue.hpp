// The speak-queue contract. Decides WHAT is said and WHEN; audio_out.hpp decides HOW.
//
// Rules (unit-tested in tests/test_speak_queue.cpp):
//   1. Priority: Safety > Name > Cue. FIFO within a priority.
//   2. Safety items are spoken even while someone is talking (they interrupt).
//   3. Names are spoken when nothing more urgent is waiting (they do not wait for silence).
//   4. Cues wait until no one is talking; a cue older than cue_max_age_ms (3 s) is dropped,
//      never spoken late; at most one cue per cue_min_interval_ms (10 s), counted from the last
//      cue actually spoken.
//   5. Bounded: when full, the lowest-priority oldest item is evicted; safety is never evicted
//      by a lower priority item.
// The queue is pure logic with an injected clock (now_ms), so it is deterministic in tests.
#pragma once

#include <cstddef>
#include <cstdint>
#include <deque>
#include <mutex>
#include <optional>
#include <string>

namespace brim::audio {

enum class Priority { Safety = 0, Name = 1, Cue = 2 };
const char* to_string(Priority p);

struct SpeechItem {
  Priority priority = Priority::Cue;
  std::string text;        // what to say (TTS fallback / log)
  std::string clip;        // optional pre-rendered WAV in audio/ (preferred: no TTS latency)
  std::int64_t created_ms = 0;  // monotonic ms when the triggering event happened
};

struct SpeakQueueConfig {
  std::int64_t cue_max_age_ms = 3000;
  std::int64_t cue_min_interval_ms = 10000;
  std::size_t capacity = 32;
};

class SpeakQueue {
 public:
  explicit SpeakQueue(SpeakQueueConfig cfg = {}) : cfg_(cfg) {}

  // Returns false if the item was rejected (queue full of higher-priority items).
  bool push(SpeechItem item);

  // Next item to speak now, or nullopt if nothing is eligible.
  // someone_talking: the "no one talking" signal from voice activity detection (inverted).
  std::optional<SpeechItem> pop_next(std::int64_t now_ms, bool someone_talking);

  std::size_t size() const;
  std::uint64_t dropped_stale_cues() const { return dropped_stale_; }
  std::uint64_t evicted() const { return evicted_; }
  void set_config(SpeakQueueConfig cfg);

 private:
  void drop_stale_cues(std::int64_t now_ms);

  SpeakQueueConfig cfg_;
  mutable std::mutex mu_;
  std::deque<SpeechItem> q_[3];
  std::optional<std::int64_t> last_cue_spoken_ms_;
  std::uint64_t dropped_stale_ = 0;
  std::uint64_t evicted_ = 0;
};

}  // namespace brim::audio
