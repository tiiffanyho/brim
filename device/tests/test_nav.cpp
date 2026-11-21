// Clear-path decisions from made-up boxes. Thresholds come from the shipped config/nav.json.
#include <string>
#include <vector>

#include "common/config.hpp"
#include "mini_test.hpp"
#include "nav/nav.hpp"

using namespace brim;
using detect::Box;
using detect::Detection;
using detect::Thing;
namespace s = brim::schema;

namespace {

const config::NavThresholds& cfg() {
  static const config::NavThresholds t = config::load_nav(std::string(BRIM_REPO_ROOT) + "/config/nav.json");
  return t;
}
constexpr double kMinConf = 0.60;

Detection det(Thing t, float x, float y, float w, float h, float score = 0.9f) {
  Detection d;
  d.thing = t;
  d.box = Box{x, y, w, h};
  d.score = score;
  return d;
}
// Close chairs (area ~0.09-0.10, well above clear_max_area_frac) standing on the floor.
Detection chair_left() { return det(Thing::Chair, 0.05f, 0.45f, 0.22f, 0.45f); }
Detection chair_centre() { return det(Thing::Chair, 0.40f, 0.50f, 0.20f, 0.45f); }
Detection chair_right() { return det(Thing::Chair, 0.73f, 0.45f, 0.22f, 0.45f); }

// Feeds the same boxes for `frames` frames at 15 fps; returns every call that was made.
std::vector<s::ClearPathEvent> feed(nav::Navigator& n, const std::vector<Detection>& dets, int frames, std::int64_t& t) {
  std::vector<s::ClearPathEvent> out;
  for (int i = 0; i < frames; ++i, t += 66)
    if (auto e = n.update(dets, t)) out.push_back(*e);
  return out;
}

bool has(const std::optional<std::vector<s::Direction>>& v, s::Direction d) {
  if (!v) return false;
  for (auto x : *v)
    if (x == d) return true;
  return false;
}

}  // namespace

TEST_CASE("config: nav thresholds are read from config/nav.json") {
  CHECK(cfg().side_margin_frac > 0.2 && cfg().side_margin_frac < 0.4);
  CHECK(cfg().clear_max_area_frac > 0 && cfg().clear_max_area_frac < 0.2);
  CHECK(cfg().clear_min_width_frac > 0);
  CHECK(cfg().decision_hold_ms > 0);
  CHECK(cfg().confirm_frames >= 2 && cfg().confirm_frames <= 3);
}

TEST_CASE("a chair on the left gives 'step right'") {
  nav::Navigator n(cfg(), kMinConf);
  std::int64_t t = 0;
  auto calls = feed(n, {chair_left()}, 10, t);
  REQUIRE(calls.size() == 1u);  // said once, not every frame
  CHECK(calls[0].clear);
  CHECK(calls[0].direction == s::Direction::Right);
  CHECK_EQ(calls[0].text, std::string("Chair on your left, step right"));
  CHECK(has(calls[0].blocked, s::Direction::Left));
}

TEST_CASE("a chair on the right gives 'step left'") {
  nav::Navigator n(cfg(), kMinConf);
  std::int64_t t = 0;
  auto calls = feed(n, {chair_right()}, 10, t);
  REQUIRE(calls.size() == 1u);
  CHECK(calls[0].direction == s::Direction::Left);
  CHECK_EQ(calls[0].text, std::string("Chair on your right, step left"));
}

TEST_CASE("a chair ahead with one clear side gives that side") {
  nav::Navigator n(cfg(), kMinConf);
  std::int64_t t = 0;
  auto calls = feed(n, {chair_centre(), det(Thing::Table, 0.70f, 0.50f, 0.25f, 0.40f)}, 10, t);
  REQUIRE(calls.size() == 1u);
  CHECK(calls[0].clear);
  CHECK(calls[0].direction == s::Direction::Left);
  CHECK_EQ(calls[0].text, std::string("Chair ahead, step left"));
  CHECK(has(calls[0].blocked, s::Direction::Ahead));
  CHECK(has(calls[0].blocked, s::Direction::Right));

  nav::Navigator n2(cfg(), kMinConf);
  t = 0;
  calls = feed(n2, {chair_centre(), chair_left()}, 10, t);
  REQUIRE(calls.size() == 1u);
  CHECK(calls[0].direction == s::Direction::Right);
  CHECK_EQ(calls[0].text, std::string("Chair ahead, step right"));
}

TEST_CASE("a chair ahead with both sides clear steps away from where it stands") {
  nav::Navigator n(cfg(), kMinConf);
  std::int64_t t = 0;
  auto calls = feed(n, {det(Thing::Chair, 0.45f, 0.50f, 0.20f, 0.45f)}, 10, t);  // a bit right of centre
  REQUIRE(calls.size() == 1u);
  CHECK(calls[0].direction == s::Direction::Left);
}

TEST_CASE("a chair in all three bands gives 'Path unclear, stop'") {
  nav::Navigator n(cfg(), kMinConf);
  std::int64_t t = 0;
  auto calls = feed(n, {chair_left(), chair_centre(), chair_right()}, 10, t);
  REQUIRE(calls.size() == 1u);
  CHECK(!calls[0].clear);
  CHECK(calls[0].direction == s::Direction::Unknown);
  CHECK_EQ(calls[0].text, std::string("Path unclear, stop"));
  CHECK(has(calls[0].blocked, s::Direction::Left));
  CHECK(has(calls[0].blocked, s::Direction::Ahead));
  CHECK(has(calls[0].blocked, s::Direction::Right));

  // One wide thing across the whole frame is the same: stop.
  nav::Navigator n2(cfg(), kMinConf);
  calls = feed(n2, {det(Thing::Table, 0.0f, 0.4f, 1.0f, 0.5f)}, 10, t);
  REQUIRE(calls.size() == 1u);
  CHECK_EQ(calls[0].text, std::string("Path unclear, stop"));
}

TEST_CASE("a tiny far chair gives no call") {
  nav::Navigator n(cfg(), kMinConf);
  std::int64_t t = 0;
  CHECK_EQ(feed(n, {det(Thing::Chair, 0.45f, 0.60f, 0.10f, 0.15f)}, 30, t).size(), 0u);  // area 0.015
}

TEST_CASE("a one-frame blip gives no call") {
  nav::Navigator n(cfg(), kMinConf);
  std::int64_t t = 0;
  CHECK_EQ(feed(n, {}, 5, t).size(), 0u);
  CHECK_EQ(feed(n, {chair_left(), chair_centre(), chair_right()}, 1, t).size(), 0u);
  CHECK_EQ(feed(n, {}, 10, t).size(), 0u);
  CHECK_EQ(feed(n, {chair_left()}, 1, t).size(), 0u);
  CHECK_EQ(feed(n, {}, 10, t).size(), 0u);
}

TEST_CASE("a call needs confirm_frames matching frames") {
  nav::Navigator n(cfg(), kMinConf);
  std::int64_t t = 0;
  CHECK_EQ(feed(n, {chair_left()}, cfg().confirm_frames - 1, t).size(), 0u);
  CHECK_EQ(feed(n, {chair_left()}, 1, t).size(), 1u);
}

TEST_CASE("a decision is held for decision_hold_ms; stop is never held back") {
  nav::Navigator n(cfg(), kMinConf);
  std::int64_t t = 0;
  REQUIRE(feed(n, {chair_left()}, 5, t).size() == 1u);  // step right
  // The chair is now on the right: the opposite advice waits for the hold to run out.
  const std::int64_t said_at = t - 5 * 66 + (cfg().confirm_frames - 1) * 66;
  std::vector<s::ClearPathEvent> calls;
  while (t - said_at < cfg().decision_hold_ms - 66)
    for (auto& e : feed(n, {chair_right()}, 1, t)) calls.push_back(e);
  CHECK_EQ(calls.size(), 0u);
  calls = feed(n, {chair_right()}, 5, t);
  REQUIRE(calls.size() == 1u);
  CHECK(calls[0].direction == s::Direction::Left);

  // Everything blocked: stop at once, without waiting for the hold.
  calls = feed(n, {chair_left(), chair_centre(), chair_right()}, cfg().confirm_frames, t);
  REQUIRE(calls.size() == 1u);
  CHECK_EQ(calls[0].text, std::string("Path unclear, stop"));
}

TEST_CASE("frames that keep disagreeing mean we are unsure: say stop, never a guess") {
  nav::Navigator n(cfg(), kMinConf);
  std::int64_t t = 0;
  std::vector<s::ClearPathEvent> calls;
  for (int i = 0; i < 20; ++i)
    for (auto& e : feed(n, {i % 2 ? chair_left() : chair_right()}, 1, t)) calls.push_back(e);
  REQUIRE(!calls.empty());
  for (const auto& e : calls) {
    CHECK(!e.clear);
    CHECK_EQ(e.text, std::string("Path unclear, stop"));
  }
}

TEST_CASE("phones and unsure boxes are never obstacles; unknown things are 'something'") {
  nav::Navigator n(cfg(), kMinConf);
  std::int64_t t = 0;
  CHECK_EQ(feed(n, {det(Thing::Phone, 0.35f, 0.5f, 0.3f, 0.45f)}, 10, t).size(), 0u);
  CHECK_EQ(feed(n, {det(Thing::Chair, 0.35f, 0.5f, 0.3f, 0.45f, 0.40f)}, 10, t).size(), 0u);
  auto calls = feed(n, {det(Thing::Unknown, 0.05f, 0.45f, 0.22f, 0.45f)}, 10, t);
  REQUIRE(calls.size() == 1u);
  CHECK_EQ(calls[0].text, std::string("Something on your left, step right"));
}

TEST_CASE("a traffic light is never an obstacle: no call, and it never changes a call") {
  nav::Navigator n(cfg(), kMinConf);
  std::int64_t t = 0;
  // A big, confident traffic light right in the corridor (a screen at hat height, close up).
  const auto light = det(Thing::TrafficLight, 0.35f, 0.40f, 0.30f, 0.55f, 0.95f);
  CHECK_EQ(feed(n, {light}, 10, t).size(), 0u);
  CHECK(nav::decide_frame({light}, cfg(), kMinConf).kind == nav::FrameDecision::Kind::NoCall);
  // With a chair on the left the advice is exactly what the chair alone gives.
  auto calls = feed(n, {chair_left(), light}, 10, t);
  REQUIRE(calls.size() == 1u);
  CHECK_EQ(calls[0].text, std::string("Chair on your left, step right"));
}

TEST_CASE("obstacles on both sides with a gap in the middle: clear path ahead") {
  nav::Navigator n(cfg(), kMinConf);
  std::int64_t t = 0;
  auto calls = feed(n, {chair_left(), chair_right()}, 10, t);
  REQUIRE(calls.size() == 1u);
  CHECK(calls[0].clear);
  CHECK(calls[0].direction == s::Direction::Ahead);
  CHECK_EQ(calls[0].text, std::string("Clear path ahead"));
}

MINI_TEST_MAIN()
