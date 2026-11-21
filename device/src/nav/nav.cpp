#include "nav/nav.hpp"

#include <algorithm>
#include <cctype>
#include <string>

namespace brim::nav {

namespace s = brim::schema;
using detect::Box;
using detect::Detection;
using detect::Thing;

namespace {

double sane_margin(double m) { return m <= 0 || m >= 0.5 ? 0.33 : m; }

double x_overlap(const Box& b, double lo, double hi) {
  return std::max(0.0, std::min<double>(b.x + b.w, hi) - std::max<double>(b.x, lo));
}

// Width of [lo, hi] not covered by any of the boxes (union of their x-intervals).
double free_width(const std::vector<const Detection*>& blockers, double lo, double hi) {
  // At most a few dozen boxes per frame: a simple sweep over sorted intervals is plenty.
  std::vector<std::pair<double, double>> iv;
  iv.reserve(blockers.size());
  for (const auto* d : blockers) {
    const double a = std::max<double>(d->box.x, lo), b = std::min<double>(d->box.x + d->box.w, hi);
    if (b > a) iv.emplace_back(a, b);
  }
  std::sort(iv.begin(), iv.end());
  double covered = 0, end = lo;
  for (const auto& [a, b] : iv) {
    const double start = std::max(a, end);
    if (b > start) {
      covered += b - start;
      end = b;
    }
  }
  return (hi - lo) - covered;
}

const Detection* biggest(const std::vector<const Detection*>& ds) {
  const Detection* best = nullptr;
  for (const auto* d : ds)
    if (!best || d->box.area() > best->box.area()) best = d;
  return best;
}

bool contains(const std::vector<s::Direction>& v, s::Direction d) { return std::find(v.begin(), v.end(), d) != v.end(); }

}  // namespace

FrameDecision decide_frame(const std::vector<Detection>& dets, const config::NavThresholds& t, double min_confidence) {
  FrameDecision out;
  const double m = sane_margin(t.side_margin_frac);

  // Only things that are big enough to be close count. Closeness is box area: no distance sensor.
  std::vector<const Detection*> blockers;
  for (const auto& d : dets) {
    if (d.thing == Thing::Phone) continue;          // watched by safety, never an obstacle
    if (d.score < min_confidence) continue;         // never act on what we did not clearly see
    if (d.box.area() <= t.clear_max_area_frac) continue;  // small = far: not in the way yet
    blockers.push_back(&d);
  }
  if (blockers.empty()) return out;  // NoCall

  // Ahead: anything overlapping the corridor (middle band, lower half: the floor in front).
  std::vector<const Detection*> in_corridor, in_left, in_right;
  for (const auto* d : blockers) {
    const bool lower_half = d->box.y + d->box.h > 0.5f;
    if (lower_half && x_overlap(d->box, m, 1.0 - m) > 0) in_corridor.push_back(d);
    if (x_overlap(d->box, 0.0, m) > 0) in_left.push_back(d);
    if (x_overlap(d->box, 1.0 - m, 1.0) > 0) in_right.push_back(d);
  }
  const bool ahead_blocked = !in_corridor.empty();
  const double left_free = free_width(in_left, 0.0, m);
  const double right_free = free_width(in_right, 1.0 - m, 1.0);
  const bool left_clear = left_free >= t.clear_min_width_frac;
  const bool right_clear = right_free >= t.clear_min_width_frac;

  if (ahead_blocked) {
    out.blocked.push_back(s::Direction::Ahead);
    out.because = biggest(in_corridor)->thing;
    if (!left_clear && !right_clear) {
      out.kind = FrameDecision::Kind::Stop;
      out.blocked = {s::Direction::Left, s::Direction::Ahead, s::Direction::Right};
      return out;
    }
    out.kind = FrameDecision::Kind::Go;
    if (left_clear && right_clear) {
      // Both sides are free: step away from where the obstacle sits (right on an exact tie).
      const Box& b = biggest(in_corridor)->box;
      out.direction = b.cx() > 0.5f ? s::Direction::Left : s::Direction::Right;
    } else {
      out.direction = left_clear ? s::Direction::Left : s::Direction::Right;
      out.blocked.push_back(left_clear ? s::Direction::Right : s::Direction::Left);
    }
    return out;
  }

  // Ahead is clear.
  if (left_clear && right_clear) return out;  // nothing near our path: no call
  out.kind = FrameDecision::Kind::Go;
  if (!left_clear && !right_clear) {
    // Obstacles on both sides with a gap in the middle: keep going straight.
    out.direction = s::Direction::Ahead;
    out.blocked = {s::Direction::Left, s::Direction::Right};
    out.because = biggest(blockers)->thing;
    return out;
  }
  // Something close on one side: move away from it, towards the side we know is clear.
  out.direction = left_clear ? s::Direction::Left : s::Direction::Right;
  out.blocked.push_back(left_clear ? s::Direction::Right : s::Direction::Left);
  out.because = biggest(left_clear ? in_right : in_left)->thing;
  return out;
}

std::string spoken_name(Thing t) {
  std::string name = t == Thing::Unknown ? "something" : detect::to_string(t);
  name[0] = static_cast<char>(std::toupper(static_cast<unsigned char>(name[0])));
  return name;
}

std::string text_for(const FrameDecision& d) {
  if (d.kind == FrameDecision::Kind::Stop) return d.because == Thing::Person ? kPersonStop : kPathUnclear;
  if (d.kind == FrameDecision::Kind::NoCall) return kPathUnclear;
  if (d.direction == s::Direction::Ahead) return "Clear path ahead";
  const std::string name = spoken_name(d.because);
  const char* move = d.direction == s::Direction::Left ? "move left" : "move right";
  if (contains(d.blocked, s::Direction::Ahead)) return name + " ahead, " + move;
  const char* side = d.direction == s::Direction::Left ? "on your right" : "on your left";
  return name + " " + side + ", " + move;
}

std::string warning_text(Thing close, const FrameDecision* current) {
  // Only a confirmed call that goes round something in front gives a side; anything else is stop.
  const bool has_side = current && current->kind == FrameDecision::Kind::Go &&
                        (current->direction == s::Direction::Left || current->direction == s::Direction::Right) &&
                        contains(current->blocked, s::Direction::Ahead);
  if (!has_side) return close == Thing::Person ? kPersonStop : kPathUnclear;
  return spoken_name(close) + " ahead, " + (current->direction == s::Direction::Left ? "move left" : "move right");
}

void Navigator::reset() {
  candidate_ = FrameDecision{};
  streak_ = 0;
  busy_frames_ = 0;
  has_current_ = false;
  current_ = FrameDecision{};
  current_since_ms_ = 0;
}

std::optional<s::ClearPathEvent> Navigator::announce(const FrameDecision& d, std::int64_t now_ms) {
  s::ClearPathEvent e;
  e.clear = d.kind == FrameDecision::Kind::Go;
  e.direction = e.clear ? d.direction : s::Direction::Unknown;
  e.text = text_for(d);
  if (!d.blocked.empty()) e.blocked = d.blocked;
  current_ = d;
  has_current_ = true;
  current_since_ms_ = now_ms;
  return e;
}

std::optional<s::ClearPathEvent> Navigator::update(const std::vector<Detection>& dets, std::int64_t now_ms) {
  const int confirm = std::clamp(t_.confirm_frames, 2, 3);
  const FrameDecision d = decide_frame(dets, t_, min_conf_);
  if (streak_ > 0 && d.same_call(candidate_)) ++streak_;
  else streak_ = 1;
  candidate_ = d;  // keep the latest details (name, blocked) for the text
  const bool confirmed = streak_ >= confirm;

  // Obstacles are there but frames keep disagreeing: we are not sure, so we say stop.
  busy_frames_ = (d.kind != FrameDecision::Kind::NoCall && !confirmed) ? busy_frames_ + 1 : 0;
  if (busy_frames_ >= 3 * confirm) {
    busy_frames_ = 0;
    if (has_current_ && current_.kind == FrameDecision::Kind::Stop) return std::nullopt;
    FrameDecision stop;
    stop.kind = FrameDecision::Kind::Stop;
    stop.blocked = {s::Direction::Left, s::Direction::Ahead, s::Direction::Right};
    return announce(stop, now_ms);
  }

  if (!confirmed) return std::nullopt;
  if (d.kind == FrameDecision::Kind::NoCall) {  // the way has been clear for a few frames: forget the old call
    has_current_ = false;
    return std::nullopt;
  }
  if (has_current_ && current_.same_call(d)) return std::nullopt;  // already said
  // Hold the last call so advice does not flip every frame. Stop is never held back: it is the safe call.
  if (has_current_ && now_ms - current_since_ms_ < t_.decision_hold_ms && d.kind != FrameDecision::Kind::Stop)
    return std::nullopt;
  return announce(d, now_ms);
}

}  // namespace brim::nav
