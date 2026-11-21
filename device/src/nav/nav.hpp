// Navigation: after a warning, the safest way to move — clear ahead, step left, step right — or
// "Path unclear, stop" when no direction is safe.
//
// The camera is the only sensor (no distance sensor), so "in the way" is judged from boxes alone:
//   * the corridor is the floor in front of the wearer: the middle band of the frame
//     (side_margin_frac .. 1 - side_margin_frac), lower half (y >= 0.5);
//   * a box blocks only if it is big enough to be close: Box::area() > clear_max_area_frac;
//   * ahead is blocked by any such box that overlaps the corridor;
//   * a side (left / right band) is clear only if the blocking boxes in it leave at least
//     clear_min_width_frac of the frame's width free.
//
// Rule: never give a direction we are not sure of. A call is made only after confirm_frames
// matching frames in a row, then held for decision_hold_ms so advice does not flip every frame.
// When there are obstacles but no decision settles, or nothing is clear, we say stop.
#pragma once

#include <cstdint>
#include <optional>
#include <vector>

#include "common/config.hpp"
#include "detect/detector.hpp"
#include "events.gen.hpp"

namespace brim::nav {

inline constexpr const char* kPathUnclear = "Path unclear, stop";

// One frame's verdict, before any smoothing.
struct FrameDecision {
  enum class Kind { NoCall, Go, Stop };
  Kind kind = Kind::NoCall;
  schema::Direction direction = schema::Direction::Unknown;  // only for Go
  std::vector<schema::Direction> blocked;
  detect::Thing because = detect::Thing::Unknown;  // the biggest blocking thing, for the spoken text
  bool same_call(const FrameDecision& o) const { return kind == o.kind && direction == o.direction; }
};

// Pure per-frame geometry. Phones are ignored (never obstacles); boxes below min_confidence are
// ignored (never act on what we did not clearly see).
FrameDecision decide_frame(const std::vector<detect::Detection>& dets, const config::NavThresholds& t,
                           double min_confidence);

// The words for a decision, e.g. "Chair ahead, step left", "Path unclear, stop".
std::string text_for(const FrameDecision& d);

// Smoothing over frames: confirmation, hold, and "unsure means stop".
class Navigator {
 public:
  Navigator(const config::NavThresholds& t, double min_confidence) : t_(t), min_conf_(min_confidence) {}

  // Call once per detected frame. Returns an event to emit and speak (Priority::Nav), or nothing.
  std::optional<schema::ClearPathEvent> update(const std::vector<detect::Detection>& dets, std::int64_t now_ms);
  void reset();

 private:
  std::optional<schema::ClearPathEvent> announce(const FrameDecision& d, std::int64_t now_ms);

  config::NavThresholds t_;
  double min_conf_;
  FrameDecision candidate_;    // what recent frames agree on
  int streak_ = 0;             // how many frames in a row agree on candidate_
  int busy_frames_ = 0;        // frames in a row with obstacles but no settled call
  bool has_current_ = false;   // something has been announced and still stands
  FrameDecision current_;
  std::int64_t current_since_ms_ = 0;
};

}  // namespace brim::nav
