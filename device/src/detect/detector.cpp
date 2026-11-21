#include "detect/detector.hpp"

#include <algorithm>
#include <cmath>

#include "common/log.hpp"

namespace brim::detect {

float iou(const Box& a, const Box& b) {
  const float x1 = std::max(a.x, b.x);
  const float y1 = std::max(a.y, b.y);
  const float x2 = std::min(a.x + a.w, b.x + b.w);
  const float y2 = std::min(a.y + a.h, b.y + b.h);
  const float inter = std::max(0.0f, x2 - x1) * std::max(0.0f, y2 - y1);
  const float uni = a.area() + b.area() - inter;
  return uni <= 0 ? 0.0f : inter / uni;
}

const char* to_string(Thing t) {
  switch (t) {
    case Thing::Person: return "person";
    case Thing::Phone: return "phone";
    case Thing::Chair: return "chair";
    case Thing::Table: return "table";
    case Thing::Door: return "door";
    case Thing::Pole: return "pole";
    case Thing::Stairs: return "stairs";
    case Thing::Vehicle: return "vehicle";
    case Thing::Bicycle: return "bicycle";
    case Thing::Unknown: return "obstacle";
  }
  return "obstacle";
}

std::optional<schema::ObstacleKind> obstacle_kind_of(Thing t) {
  switch (t) {
    case Thing::Person: return schema::ObstacleKind::Person;
    case Thing::Chair: return schema::ObstacleKind::Chair;
    case Thing::Table: return schema::ObstacleKind::Table;
    case Thing::Door: return schema::ObstacleKind::Door;
    case Thing::Pole: return schema::ObstacleKind::Pole;
    case Thing::Stairs: return schema::ObstacleKind::Stairs;
    case Thing::Vehicle: return schema::ObstacleKind::Vehicle;
    case Thing::Bicycle: return schema::ObstacleKind::Bicycle;
    case Thing::Unknown: return schema::ObstacleKind::Obstacle;  // "something is there"
    case Thing::Phone: return std::nullopt;                      // never an obstacle
  }
  return std::nullopt;
}

schema::Direction direction_of(const Box& b, double side_margin_frac) {
  const double m = side_margin_frac <= 0 || side_margin_frac >= 0.5 ? 0.33 : side_margin_frac;
  const double c = b.cx();
  if (c < m) return schema::Direction::Left;
  if (c > 1.0 - m) return schema::Direction::Right;
  return schema::Direction::Ahead;
}

schema::Proximity proximity_of(double area_frac, double close_frac, double near_frac) {
  if (area_frac <= 0) return schema::Proximity::Unknown;
  if (area_frac >= close_frac) return schema::Proximity::Close;
  if (area_frac >= near_frac) return schema::Proximity::Near;
  return schema::Proximity::Far;
}

namespace {
class StubDetector final : public Detector {
 public:
  bool load() override { return true; }
  std::vector<Detection> detect(const capture::Frame&) override { return {}; }
  const char* name() const override { return "stub"; }
  bool healthy() const override { return true; }
};
}  // namespace

std::unique_ptr<Detector> make_stub_detector() { return std::make_unique<StubDetector>(); }

std::unique_ptr<Detector> make_mediapipe_detector(const std::string& models_dir) {
  BRIM_LOGW("detect", "MediaPipe detector not implemented yet (models_dir=%s); using stub", models_dir.c_str());  // TODO(ai)
  return make_stub_detector();
}

// ------------------------------------------------------------------ tracks

float TrackStore::match_score(const Box& t, const Box& d) const {
  const float ov = iou(t, d);
  if (ov >= same_iou_) return 1.0f + ov;
  const float ta = t.area(), da = d.area();
  if (ta <= 0 || da <= 0) return 0;
  if (std::min(ta, da) / std::max(ta, da) < 0.5f) return 0;  // very different size: not the same thing
  const float ref = std::max(std::max(t.w, t.h), std::max(d.w, d.h));
  const float dist = std::hypot(t.cx() - d.cx(), t.cy() - d.cy()) / ref;
  if (dist >= center_gate_) return 0;
  return 1.0f - dist / center_gate_ * 0.999f;  // in (0, 1]: always below any overlap match
}

void TrackStore::observe(std::vector<Detection>& dets, std::int64_t now_ms) {
  expire(now_ms);
  ++observe_count_;
  pairs_.clear();
  det_taken_.assign(dets.size(), 0);
  for (int i = 0; i < static_cast<int>(dets.size()); ++i) {
    for (const auto& [id, t] : tracks_) {
      if (t.thing != dets[i].thing) continue;
      if (now_ms - t.last_seen_ms > match_window_ms_) continue;  // gone too long: a new track
      const float s = match_score(t.box, dets[i].box);
      if (s > 0) pairs_.push_back(Pair{s, i, id});
    }
  }
  std::sort(pairs_.begin(), pairs_.end(), [](const Pair& a, const Pair& b) {
    if (a.score != b.score) return a.score > b.score;
    if (a.det != b.det) return a.det < b.det;
    return a.id < b.id;
  });
  for (const auto& p : pairs_) {
    if (det_taken_[p.det]) continue;
    Track& t = tracks_[p.id];
    if (t.matched_in_ == observe_count_) continue;
    t.matched_in_ = observe_count_;
    t.box = dets[p.det].box;
    t.last_seen_ms = now_ms;
    det_taken_[p.det] = 1;
    dets[p.det].track_id = p.id;
  }
  for (int i = 0; i < static_cast<int>(dets.size()); ++i) {
    if (det_taken_[i]) continue;
    const int id = next_id_++;
    Track t;
    t.thing = dets[i].thing;
    t.box = dets[i].box;
    t.last_seen_ms = now_ms;
    t.matched_in_ = observe_count_;
    tracks_[id] = t;
    dets[i].track_id = id;
  }
}

std::size_t TrackStore::active(std::int64_t now_ms) const {
  std::size_t n = 0;
  for (const auto& kv : tracks_)
    if (now_ms - kv.second.last_seen_ms <= match_window_ms_) ++n;
  return n;
}

void TrackStore::expire(std::int64_t now_ms) {
  for (auto it = tracks_.begin(); it != tracks_.end();) {
    if (now_ms - it->second.last_seen_ms >= forget_after_ms_) it = tracks_.erase(it);
    else ++it;
  }
}

bool TrackStore::should_announce(int track_id, std::int64_t now_ms) {
  auto it = tracks_.find(track_id);
  if (it == tracks_.end()) return false;
  if (it->second.last_announced_ms >= 0 && now_ms - it->second.last_announced_ms < repeat_suppress_ms_) return false;
  it->second.last_announced_ms = now_ms;
  return true;
}

void TrackStore::clear() { tracks_.clear(); }

}  // namespace brim::detect
