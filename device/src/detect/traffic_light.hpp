// Traffic light FEASIBILITY TEST (branch feat/traffic-light-test). Not a crossing feature.
//
// Hard rules, enforced here and in tests/test_traffic.cpp:
//   1. Brim never says walk, cross, go or safe to cross. It reports what it sees, nothing more.
//   2. The only phrases are kPhraseRed / kPhraseGreen / kPhraseUnknown below.
//   3. A colour is reported only when the colour check is confident; otherwise "colour unknown".
//   4. The cue never has a direction (TrafficLightSeenEvent has no direction field) and goes out
//      at navigation priority, never safety, so it can never replace or delay a safety alert.
// A traffic light is never an obstacle (detect::is_obstacle): it stays out of src/nav and src/safety.
//
// The colour check mirrors scripts/test_traffic.py exactly (same HSV maths, same pixel bounds,
// same config/traffic.json) so the laptop photo test predicts what the hat will say.
#pragma once

#include <cstdint>
#include <map>

#include "common/config.hpp"
#include "detect/detector.hpp"
#include "events.gen.hpp"

namespace brim::detect {

inline constexpr const char* kPhraseRed = "Traffic light ahead, red";
inline constexpr const char* kPhraseGreen = "Traffic light ahead, green";
inline constexpr const char* kPhraseUnknown = "Traffic light ahead, colour unknown";

// The words for a colour: always one of the three phrases above.
const char* traffic_phrase(schema::TrafficColour c);

struct LightColour {
  schema::TrafficColour colour = schema::TrafficColour::Unknown;
  int red = 0;      // bright red-hue pixels in the box
  int green = 0;    // bright green-hue pixels in the box
  int total = 0;    // pixels in the box
  double share = 0; // winning colour's pixels / total (0 when unknown)
};

// OpenCV-convention HSV of one RGB pixel: h 0-179, s and v 0-255.
void rgb_to_hsv(std::uint8_t r, std::uint8_t g, std::uint8_t b, int& h, int& s, int& v);

// Counts bright red-hue and green-hue pixels inside `box` (normalized, on a packed RGB888 image of
// width x height). The colour with the larger count wins only if it covers at least min_share of
// the box and has min_dominance times the other's count; anything else is Unknown. No allocation.
LightColour classify_light(const std::uint8_t* rgb, int width, int height, const Box& box,
                           const config::TrafficConfig& cfg);

// One cue per light per cue_interval_ms (keyed by track id). Forgets lights it has not cued for a
// minute, so it holds only a handful of entries.
class TrafficCueLimiter {
 public:
  explicit TrafficCueLimiter(std::int64_t interval_ms) : interval_ms_(interval_ms) {}
  bool should_cue(int track_id, std::int64_t now_ms);
  void clear() { last_.clear(); }
  std::size_t size() const { return last_.size(); }

 private:
  std::int64_t interval_ms_;
  std::map<int, std::int64_t> last_;
};

}  // namespace brim::detect
