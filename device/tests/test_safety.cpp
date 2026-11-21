// Close-in-front warnings from made-up boxes. Thresholds come from the shipped config/safety.json and
// config/nav.json. The words are one phrase with what to do, from nav::warning_text.
#include <cctype>
#include <string>
#include <vector>

#include "audio/speak_queue.hpp"
#include "common/config.hpp"
#include "mini_test.hpp"
#include "nav/nav.hpp"
#include "safety/safety_monitor.hpp"

using namespace brim;
using detect::Box;
using detect::Detection;
using detect::Thing;
namespace s = brim::schema;

namespace {

const config::SafetyThresholds& safety_cfg() {
  static const auto t = config::load_safety(std::string(BRIM_REPO_ROOT) + "/config/safety.json");
  return t;
}
const config::NavThresholds& nav_cfg() {
  static const auto t = config::load_nav(std::string(BRIM_REPO_ROOT) + "/config/nav.json");
  return t;
}

Detection det(Thing t, float x, float y, float w, float h, float score = 0.9f) {
  Detection d;
  d.thing = t;
  d.box = Box{x, y, w, h};
  d.score = score;
  return d;
}
// A person close in front (29 % of the frame), left of centre but in the corridor.
Detection close_person() { return det(Thing::Person, 0.12f, 0.15f, 0.36f, 0.80f); }
// A chair close in front (24 %).
Detection close_chair() { return det(Thing::Chair, 0.30f, 0.35f, 0.40f, 0.60f); }

nav::FrameDecision go(s::Direction d) {
  nav::FrameDecision f;
  f.kind = nav::FrameDecision::Kind::Go;
  f.direction = d;
  f.blocked = {s::Direction::Ahead};
  return f;
}

// Feeds the same boxes for `frames` frames at 15 fps; returns every alert.
std::vector<s::SafetyAlertEvent> feed(safety::SafetyMonitor& m, const std::vector<Detection>& dets, int frames,
                                      std::int64_t& t, const nav::FrameDecision* nav_now = nullptr) {
  std::vector<s::SafetyAlertEvent> out;
  capture::Frame f;
  for (int i = 0; i < frames; ++i, t += 66) {
    f.t_capture_mono_ms = t;
    for (auto& a : m.check(f, dets, nav_now)) out.push_back(a);
  }
  return out;
}

bool has_word(const std::string& text, const std::string& word) {
  std::string norm = " ";
  for (char ch : text) norm += std::isalpha(static_cast<unsigned char>(ch)) ? static_cast<char>(std::tolower(ch)) : ' ';
  norm += " ";
  return norm.find(" " + word + " ") != std::string::npos;
}

}  // namespace

TEST_CASE("a person close in front: 'Person ahead, move <side>' with the confirmed side") {
  auto m = safety::make_vision_safety_monitor(safety_cfg(), nav_cfg());
  std::int64_t t = 0;
  const auto right = go(s::Direction::Right);
  auto alerts = feed(*m, {close_person()}, 5, t, &right);
  REQUIRE(alerts.size() == 1u);  // confirmed once, then held back by repeat_suppress_s
  CHECK(alerts[0].kind == s::SafetyKind::PersonClose);
  CHECK(alerts[0].severity == s::Severity::Critical);
  CHECK(alerts[0].direction == s::Direction::Ahead);
  CHECK_EQ(alerts[0].text, std::string("Person ahead, move right"));
}

TEST_CASE("no confirmed clear side yet: 'Person ahead, stop' / 'Path unclear, stop', never a guess") {
  auto m = safety::make_vision_safety_monitor(safety_cfg(), nav_cfg());
  std::int64_t t = 0;
  auto alerts = feed(*m, {close_person(), close_chair()}, 5, t, nullptr);
  REQUIRE(alerts.size() == 2u);
  CHECK_EQ(alerts[0].text, std::string("Person ahead, stop"));
  CHECK(alerts[1].kind == s::SafetyKind::ObjectClose);
  CHECK_EQ(alerts[1].text, std::string("Path unclear, stop"));
}

TEST_CASE("a chair close in front with a clear side: 'Chair ahead, move left'") {
  auto m = safety::make_vision_safety_monitor(safety_cfg(), nav_cfg());
  std::int64_t t = 0;
  const auto left = go(s::Direction::Left);
  auto alerts = feed(*m, {close_chair()}, 5, t, &left);
  REQUIRE(alerts.size() == 1u);
  CHECK_EQ(alerts[0].text, std::string("Chair ahead, move left"));
}

TEST_CASE("one frame is a blip; small, unsure, side-only boxes and phones never warn") {
  auto m = safety::make_vision_safety_monitor(safety_cfg(), nav_cfg());
  std::int64_t t = 0;
  CHECK_EQ(feed(*m, {close_person()}, 1, t).size(), 0u);  // needs confirm_frames in a row
  CHECK_EQ(feed(*m, {}, 1, t).size(), 0u);
  CHECK_EQ(feed(*m, {close_person()}, 1, t).size(), 0u);  // the streak started again
  auto m2 = safety::make_vision_safety_monitor(safety_cfg(), nav_cfg());
  CHECK_EQ(feed(*m2, {det(Thing::Person, 0.40f, 0.55f, 0.15f, 0.30f)}, 10, t).size(), 0u);        // 4.5 %: not close
  CHECK_EQ(feed(*m2, {det(Thing::Person, 0.12f, 0.15f, 0.36f, 0.80f, 0.40f)}, 10, t).size(), 0u); // not sure it is there
  CHECK_EQ(feed(*m2, {det(Thing::Chair, 0.0f, 0.20f, 0.30f, 0.80f)}, 10, t).size(), 0u);          // left band only
  CHECK_EQ(feed(*m2, {det(Thing::Chair, 0.30f, 0.0f, 0.40f, 0.45f)}, 10, t).size(), 0u);          // upper half only
  CHECK_EQ(feed(*m2, {det(Thing::Phone, 0.30f, 0.35f, 0.40f, 0.60f)}, 10, t).size(), 0u);         // phones are never obstacles
}

TEST_CASE("repeated at most every repeat_suppress_s, then again") {
  auto m = safety::make_vision_safety_monitor(safety_cfg(), nav_cfg());
  std::int64_t t = 0;
  CHECK_EQ(feed(*m, {close_person()}, 30, t).size(), 1u);  // 2 s
  t += safety_cfg().repeat_suppress_s * 1000;
  CHECK_EQ(feed(*m, {close_person()}, 3, t).size(), 1u);
}

TEST_CASE("the warning goes before the direction it needed, through the one speak queue") {
  auto m = safety::make_vision_safety_monitor(safety_cfg(), nav_cfg());
  std::int64_t t = 0;
  const auto right = go(s::Direction::Right);
  auto alerts = feed(*m, {close_person()}, 3, t, &right);
  REQUIRE(alerts.size() == 1u);
  audio::SpeakQueue q;
  s::ClearPathEvent path;
  path.clear = true;
  path.direction = s::Direction::Right;
  path.text = "Person ahead, move right";
  q.push({audio::Priority::Nav, path.text, t, audio::CueEvent{path}});  // even if queued first
  q.push({audio::Priority::Safety, alerts[0].text, t, audio::CueEvent{alerts[0]}});
  auto first = q.pop_next(t + 10);
  REQUIRE(first.has_value());
  CHECK(first->priority == audio::Priority::Safety);
  CHECK(std::holds_alternative<s::SafetyAlertEvent>(*first->event));
  auto second = q.pop_next(t + 10);
  REQUIRE(second.has_value());
  CHECK(second->priority == audio::Priority::Nav);
}

TEST_CASE("no warning or direction phrase says walk, cross, go, someone, nearby or a vague word") {
  std::vector<std::string> phrases = {nav::kPathUnclear, nav::kPersonStop};
  for (auto d : {s::Direction::Left, s::Direction::Right}) {
    auto f = go(d);
    for (auto th : {Thing::Person, Thing::Chair, Thing::Table, Thing::Vehicle, Thing::Bicycle, Thing::Unknown}) {
      f.because = th;
      phrases.push_back(nav::warning_text(th, &f));
      phrases.push_back(nav::text_for(f));
    }
  }
  for (const auto& p : phrases)
    for (const char* banned : {"walk", "cross", "go", "safe", "someone", "nearby", "know", "step", "phone"})
      CHECK(!has_word(p, banned));
}

MINI_TEST_MAIN()
