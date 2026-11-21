#include "detect/detector.hpp"

#include <algorithm>

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

void TrackStore::observe(std::vector<Detection>& dets, std::int64_t now_ms) {
  expire(now_ms);
  std::vector<int> taken;
  taken.reserve(dets.size());
  for (auto& d : dets) {
    int best_id = -1;
    float best = same_iou_;
    for (const auto& [id, t] : tracks_) {
      if (t.thing != d.thing) continue;
      if (std::find(taken.begin(), taken.end(), id) != taken.end()) continue;
      const float s = iou(t.box, d.box);
      if (s >= best) {
        best = s;
        best_id = id;
      }
    }
    if (best_id < 0) {
      best_id = next_id_++;
      tracks_[best_id] = Track{d.thing, d.box, now_ms, -1};
    } else {
      tracks_[best_id].box = d.box;
      tracks_[best_id].last_seen_ms = now_ms;
    }
    taken.push_back(best_id);
    d.track_id = best_id;
  }
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
