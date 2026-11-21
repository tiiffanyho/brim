#include "detect/traffic_light.hpp"

#include <algorithm>
#include <cmath>

namespace brim::detect {

namespace s = brim::schema;

const char* traffic_phrase(s::TrafficColour c) {
  switch (c) {
    case s::TrafficColour::Red: return kPhraseRed;
    case s::TrafficColour::Green: return kPhraseGreen;
    case s::TrafficColour::Unknown: return kPhraseUnknown;
  }
  return kPhraseUnknown;
}

void rgb_to_hsv(std::uint8_t r, std::uint8_t g, std::uint8_t b, int& h, int& s_out, int& v) {
  const int mx = std::max({r, g, b});
  const int mn = std::min({r, g, b});
  const int d = mx - mn;
  v = mx;
  s_out = mx == 0 ? 0 : (255 * d + mx / 2) / mx;  // rounded
  if (d == 0) {
    h = 0;
    return;
  }
  double deg;
  if (mx == r) deg = 60.0 * (g - b) / d;
  else if (mx == g) deg = 120.0 + 60.0 * (b - r) / d;
  else deg = 240.0 + 60.0 * (r - g) / d;
  if (deg < 0) deg += 360.0;
  h = static_cast<int>(std::floor(deg / 2.0 + 0.5)) % 180;
}

namespace {
bool in_ranges(int h, const std::vector<config::HueRange>& rs) {
  for (const auto& r : rs)
    if (h >= r.lo && h <= r.hi) return true;
  return false;
}
// Normalized box -> pixel bounds [x0, x1) x [y0, y1): each edge rounded to the nearest pixel edge
// (floor(v + 0.5)), exactly as scripts/test_traffic.py does, so float noise in the box never
// shifts the crop by a pixel.
int edge(double frac, int size) { return std::clamp(static_cast<int>(std::floor(frac * size + 0.5)), 0, size); }
void pixel_bounds(const Box& b, int w, int h, int& x0, int& y0, int& x1, int& y1) {
  x0 = edge(b.x, w);
  y0 = edge(b.y, h);
  x1 = edge(static_cast<double>(b.x) + b.w, w);
  y1 = edge(static_cast<double>(b.y) + b.h, h);
}
}  // namespace

LightColour classify_light(const std::uint8_t* rgb, int width, int height, const Box& box,
                           const config::TrafficConfig& cfg) {
  LightColour out;
  if (!rgb || width <= 0 || height <= 0) return out;
  int x0, y0, x1, y1;
  pixel_bounds(box, width, height, x0, y0, x1, y1);
  if (x1 <= x0 || y1 <= y0) return out;
  for (int y = y0; y < y1; ++y) {
    const std::uint8_t* p = rgb + (static_cast<std::size_t>(y) * width + x0) * 3;
    for (int x = x0; x < x1; ++x, p += 3) {
      int h, s, v;
      rgb_to_hsv(p[0], p[1], p[2], h, s, v);
      if (v < cfg.min_bright || s < cfg.min_saturation) continue;
      if (in_ranges(h, cfg.red_hue)) ++out.red;
      else if (in_ranges(h, cfg.green_hue)) ++out.green;
    }
  }
  out.total = (x1 - x0) * (y1 - y0);
  const bool red_wins = out.red > out.green;
  const int win = red_wins ? out.red : out.green;
  const int lose = red_wins ? out.green : out.red;
  if (win == lose) return out;  // tie (including 0/0): not sure
  const double share = static_cast<double>(win) / out.total;
  if (share < cfg.min_share) return out;
  if (static_cast<double>(win) < cfg.min_dominance * lose) return out;
  out.colour = red_wins ? s::TrafficColour::Red : s::TrafficColour::Green;
  out.share = share;
  return out;
}

bool TrafficCueLimiter::should_cue(int track_id, std::int64_t now_ms) {
  for (auto it = last_.begin(); it != last_.end();) {
    if (now_ms - it->second > std::max<std::int64_t>(60000, interval_ms_)) it = last_.erase(it);
    else ++it;
  }
  auto it = last_.find(track_id);
  if (it != last_.end() && now_ms - it->second < interval_ms_) return false;
  last_[track_id] = now_ms;
  return true;
}

}  // namespace brim::detect
