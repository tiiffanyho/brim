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

// ------------------------------------------------------------------ model output helpers

std::optional<Thing> thing_of_coco(int c) {
  // Indices are the model's own labelmap.txt (COCO-90, 0-based, gaps kept as "???").
  switch (c) {
    case 0: return Thing::Person;
    case 1: return Thing::Bicycle;
    case 2:   // car
    case 3:   // motorcycle
    case 5:   // bus
    case 7:   // truck
      return Thing::Vehicle;
    case 61: return Thing::Chair;
    case 66: return Thing::Table;  // dining table
    case 76: return Thing::Phone;  // cell phone: watched by safety, never an obstacle
    case 14:  // bench
    case 26:  // backpack
    case 32:  // suitcase
    case 62:  // couch
      return Thing::Unknown;  // in the way, but only ever called "something"
    default: return std::nullopt;
  }
}

void RgbResizer::build(int sw, int sh, int dw, int dh) {
  sw_ = sw, sh_ = sh, dw_ = dw, dh_ = dh;
  x0_.resize(dw), x1_.resize(dw), fx_.resize(dw);
  y0_.resize(dh), y1_.resize(dh), fy_.resize(dh);
  auto axis = [](int s, int d, std::vector<int>& a0, std::vector<int>& a1, std::vector<std::uint16_t>& f, int mul) {
    const double scale = static_cast<double>(s) / d;
    for (int i = 0; i < d; ++i) {
      double src = (i + 0.5) * scale - 0.5;  // pixel-centre aligned
      if (src < 0) src = 0;
      int i0 = static_cast<int>(src);
      if (i0 > s - 1) i0 = s - 1;
      const int i1 = i0 + 1 < s ? i0 + 1 : s - 1;
      a0[i] = i0 * mul;
      a1[i] = i1 * mul;
      f[i] = static_cast<std::uint16_t>(std::lround((src - i0) * 256.0));
    }
  };
  axis(sw, dw, x0_, x1_, fx_, 3);
  axis(sh, dh, y0_, y1_, fy_, 1);
}

void RgbResizer::resize(const std::uint8_t* src, int sw, int sh, std::uint8_t* dst, int dw, int dh) {
  if (sw <= 0 || sh <= 0 || dw <= 0 || dh <= 0) return;
  if (sw != sw_ || sh != sh_ || dw != dw_ || dh != dh_) build(sw, sh, dw, dh);  // only on size change
  const int stride = sw * 3;
  for (int y = 0; y < dh; ++y) {
    const std::uint8_t* r0 = src + static_cast<std::size_t>(y0_[y]) * stride;
    const std::uint8_t* r1 = src + static_cast<std::size_t>(y1_[y]) * stride;
    const std::uint32_t wy = fy_[y], iy = 256 - wy;
    std::uint8_t* o = dst + static_cast<std::size_t>(y) * dw * 3;
    for (int x = 0; x < dw; ++x) {
      const int a = x0_[x], b = x1_[x];
      const std::uint32_t wx = fx_[x], ix = 256 - wx;
      for (int ch = 0; ch < 3; ++ch) {
        const std::uint32_t top = r0[a + ch] * ix + r0[b + ch] * wx;
        const std::uint32_t bot = r1[a + ch] * ix + r1[b + ch] * wx;
        *o++ = static_cast<std::uint8_t>((top * iy + bot * wy + 32768) >> 16);
      }
    }
  }
}

void decode_detections(const float* boxes, const float* classes, const float* scores, int count, float min_conf,
                       float phone_min_conf, std::vector<Detection>& out) {
  auto clamp01 = [](float v) { return v < 0 ? 0.0f : (v > 1 ? 1.0f : v); };
  for (int i = 0; i < count; ++i) {
    const float score = scores[i];
    if (!(score >= 0.0f && score <= 1.0f)) continue;  // NaN or garbage: never trust it
    auto thing = thing_of_coco(static_cast<int>(std::lround(classes[i])));
    if (!thing) continue;
    const float need = *thing == Thing::Phone ? phone_min_conf : min_conf;
    if (score < need) continue;
    const float* b = boxes + 4 * i;  // ymin, xmin, ymax, xmax
    const float y0 = clamp01(b[0]), x0 = clamp01(b[1]), y1 = clamp01(b[2]), x1 = clamp01(b[3]);
    if (x1 - x0 <= 0.0f || y1 - y0 <= 0.0f) continue;
    Detection d;
    d.thing = *thing;
    d.box = Box{x0, y0, x1 - x0, y1 - y0};
    d.score = score;
    out.push_back(d);
  }
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
