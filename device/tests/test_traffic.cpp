// Traffic light feasibility test: the colour check, the three allowed phrases, the cue rate limit,
// and that the cue never outranks a safety alert. Thresholds come from the shipped config/traffic.json.
#include <cctype>
#include <cstdio>
#include <fstream>
#include <string>
#include <vector>

#include <unistd.h>

#include "audio/speak_queue.hpp"
#include "common/config.hpp"
#include "detect/traffic_light.hpp"
#include "mini_test.hpp"

using namespace brim;
using detect::Box;
namespace s = brim::schema;

namespace {

const config::TrafficConfig& cfg() {
  static const config::TrafficConfig t = config::load_traffic(std::string(BRIM_REPO_ROOT) + "/config/traffic.json");
  return t;
}

// A dark grey 100x100 frame (a housing in a dim room); paint() fills a rectangle with one colour.
struct Img {
  int w = 100, h = 100;
  std::vector<std::uint8_t> px = std::vector<std::uint8_t>(100 * 100 * 3, 40);
  void paint(int x0, int y0, int x1, int y1, std::uint8_t r, std::uint8_t g, std::uint8_t b) {
    for (int y = y0; y < y1; ++y)
      for (int x = x0; x < x1; ++x) {
        std::uint8_t* p = &px[(static_cast<std::size_t>(y) * w + x) * 3];
        p[0] = r, p[1] = g, p[2] = b;
      }
  }
  detect::LightColour classify(const Box& b) const { return detect::classify_light(px.data(), w, h, b, cfg()); }
};
// The traffic light box: x 40..60, y 10..70 (1200 px).
const Box kBox{0.40f, 0.10f, 0.20f, 0.60f};

std::string lower(std::string s) {
  for (auto& ch : s) ch = static_cast<char>(std::tolower(static_cast<unsigned char>(ch)));
  return s;
}
bool has_word(const std::string& text, const std::string& word) {
  const std::string t = " " + lower(text) + " ";
  std::string norm;  // letters kept, everything else a space: whole-word search
  for (char ch : t) norm += std::isalpha(static_cast<unsigned char>(ch)) ? ch : ' ';
  return norm.find(" " + word + " ") != std::string::npos;
}

}  // namespace

TEST_CASE("only the three allowed phrases, and none says walk, cross, go or safe") {
  CHECK_EQ(std::string(detect::traffic_phrase(s::TrafficColour::Red)), "Traffic light ahead, red");
  CHECK_EQ(std::string(detect::traffic_phrase(s::TrafficColour::Green)), "Traffic light ahead, green");
  CHECK_EQ(std::string(detect::traffic_phrase(s::TrafficColour::Unknown)), "Traffic light ahead, colour unknown");
  for (auto c : {s::TrafficColour::Red, s::TrafficColour::Green, s::TrafficColour::Unknown}) {
    const std::string p = detect::traffic_phrase(c);
    for (const char* banned : {"walk", "cross", "crossing", "go", "safe", "left", "right", "step", "stop"})
      CHECK(!has_word(p, banned));
  }
}

TEST_CASE("config: thresholds come from config/traffic.json, and the cue ships off") {
  CHECK(!cfg().cue_enabled);
  CHECK(cfg().min_score >= 0.5 && cfg().min_score <= 1.0);
  CHECK_EQ(cfg().cue_interval_ms, 10000);
  CHECK(cfg().min_share > 0 && cfg().min_share < 1);
  CHECK(cfg().min_dominance >= 1);
  REQUIRE(!cfg().red_hue.empty());
  REQUIRE(!cfg().green_hue.empty());
  CHECK_EQ(cfg().red_hue[0].lo, 0);  // red wraps around hue 0
  CHECK(cfg().green_hue[0].lo > 30 && cfg().green_hue[0].hi < 100);
}

TEST_CASE("config: a broken traffic.json keeps safe defaults and the 10 s floor") {
  char dir[] = "/tmp/brim_traffic_XXXXXX";
  REQUIRE(mkdtemp(dir) != nullptr);
  const std::string path = std::string(dir) + "/traffic.json";
  {
    std::ofstream f(path);
    f << R"({"cue_enabled": true, "cue_interval_s": 2, "min_share": 0, "min_dominance": 0.5,
             "red_hue": [[170, 10]], "green_hue": "green"})";
  }
  const auto c = config::load_traffic(path);
  const config::TrafficConfig def;
  CHECK(c.cue_enabled);
  CHECK_EQ(c.cue_interval_ms, 10000);  // never more than one cue per light per 10 s
  CHECK_EQ(c.min_share, def.min_share);
  CHECK_EQ(c.min_dominance, def.min_dominance);
  REQUIRE(c.red_hue.size() == def.red_hue.size());
  CHECK_EQ(c.red_hue[0].hi, def.red_hue[0].hi);
  REQUIRE(c.green_hue.size() == def.green_hue.size());
  CHECK_EQ(c.green_hue[0].lo, def.green_hue[0].lo);
  std::remove(path.c_str());
  rmdir(dir);
  // A missing file: defaults, cue off.
  CHECK(!config::load_traffic("/nonexistent/traffic.json").cue_enabled);
}

TEST_CASE("HSV in OpenCV units (h 0-179, s and v 0-255)") {
  int h, sat, v;
  detect::rgb_to_hsv(255, 0, 0, h, sat, v);
  CHECK(h == 0 && sat == 255 && v == 255);
  detect::rgb_to_hsv(0, 255, 0, h, sat, v);
  CHECK_EQ(h, 60);
  detect::rgb_to_hsv(0, 0, 255, h, sat, v);
  CHECK_EQ(h, 120);
  detect::rgb_to_hsv(0, 255, 180, h, sat, v);  // a real "green" lamp is blue-green
  CHECK_EQ(h, 81);
  detect::rgb_to_hsv(255, 170, 0, h, sat, v);  // amber
  CHECK_EQ(h, 20);
  detect::rgb_to_hsv(255, 0, 40, h, sat, v);  // red just past 360 degrees
  CHECK_EQ(h, 175);
  detect::rgb_to_hsv(250, 250, 250, h, sat, v);  // glare: no colour at all
  CHECK(h == 0 && sat == 0 && v == 250);
  detect::rgb_to_hsv(0, 0, 0, h, sat, v);
  CHECK(h == 0 && sat == 0 && v == 0);
}

TEST_CASE("a lit red lamp is red, a lit green lamp is green") {
  Img red;
  red.paint(44, 14, 56, 30, 255, 30, 20);  // top lamp, 192 px = 16 % of the box
  auto r = red.classify(kBox);
  CHECK(r.colour == s::TrafficColour::Red);
  CHECK_EQ(r.total, 1200);
  CHECK_EQ(r.red, 192);
  CHECK(r.share > 0.15 && r.share < 0.17);

  Img green;
  green.paint(44, 50, 56, 66, 0, 230, 170);  // bottom lamp, blue-green
  auto g = green.classify(kBox);
  CHECK(g.colour == s::TrafficColour::Green);
  CHECK_EQ(g.green, 192);
}

TEST_CASE("unsure means colour unknown: off, amber, glare, too small, or mixed") {
  Img off;  // unlit lamps: dark red and dark green
  off.paint(44, 14, 56, 30, 90, 10, 10);
  off.paint(44, 50, 56, 66, 10, 90, 60);
  CHECK(off.classify(kBox).colour == s::TrafficColour::Unknown);

  Img amber;
  amber.paint(44, 32, 56, 48, 255, 170, 0);
  CHECK(amber.classify(kBox).colour == s::TrafficColour::Unknown);

  Img glare;  // the C270 often blows a lamp out to white
  glare.paint(44, 14, 56, 30, 255, 250, 245);
  CHECK(glare.classify(kBox).colour == s::TrafficColour::Unknown);

  Img tiny;  // 4x4 = 16 px of 1200, under min_share
  tiny.paint(48, 20, 52, 24, 255, 30, 20);
  auto t = tiny.classify(kBox);
  CHECK_EQ(t.red, 16);
  CHECK(t.colour == s::TrafficColour::Unknown);
  CHECK_EQ(t.share, 0.0);

  Img mixed;  // both lit, red only 1.5x green: not dominant enough
  mixed.paint(44, 14, 56, 30, 255, 30, 20);    // 192 red
  mixed.paint(44, 50, 52, 66, 0, 230, 170);    // 128 green
  CHECK(mixed.classify(kBox).colour == s::TrafficColour::Unknown);

  Img tie;
  tie.paint(44, 14, 56, 30, 255, 30, 20);
  tie.paint(44, 50, 56, 66, 0, 230, 170);
  CHECK(tie.classify(kBox).colour == s::TrafficColour::Unknown);
}

TEST_CASE("the colour check only looks inside the box") {
  Img img;
  img.paint(0, 0, 30, 100, 255, 30, 20);  // a red wall to the left, outside the box
  CHECK_EQ(img.classify(kBox).red, 0);
  CHECK(img.classify(kBox).colour == s::TrafficColour::Unknown);
  // Degenerate or off-frame boxes and missing pixels are unknown, never a guess.
  CHECK(img.classify(Box{0.5f, 0.5f, 0.0f, 0.0f}).colour == s::TrafficColour::Unknown);
  CHECK(img.classify(Box{1.2f, 1.2f, 0.1f, 0.1f}).colour == s::TrafficColour::Unknown);
  CHECK(detect::classify_light(nullptr, 100, 100, kBox, cfg()).colour == s::TrafficColour::Unknown);
}

TEST_CASE("one cue per light every 10 s; each light counts on its own") {
  detect::TrafficCueLimiter lim(cfg().cue_interval_ms);
  CHECK(lim.should_cue(1, 0));
  CHECK(!lim.should_cue(1, 5000));
  CHECK(!lim.should_cue(1, 9999));
  CHECK(lim.should_cue(2, 9999));  // another light
  CHECK(lim.should_cue(1, 10000));
  CHECK(!lim.should_cue(1, 19999));
  CHECK(lim.should_cue(1, 20000));
  lim.should_cue(3, 21000);
  CHECK(lim.should_cue(9, 200000));  // long gone lights are forgotten
  CHECK_EQ(lim.size(), 1u);
}

TEST_CASE("a traffic light cue never goes before a safety alert, and is never sent late") {
  audio::SpeakQueue q({3000, 10000, 32});
  s::TrafficLightSeenEvent tl;
  tl.colour = s::TrafficColour::Red;
  tl.text = detect::traffic_phrase(tl.colour);
  q.push({audio::Priority::Nav, tl.text, 0, audio::CueEvent{tl}});
  s::SafetyAlertEvent a;
  a.text = "Person close ahead";
  q.push({audio::Priority::Safety, a.text, 10, audio::CueEvent{a}});
  auto first = q.pop_next(20);
  REQUIRE(first.has_value());
  CHECK(first->priority == audio::Priority::Safety);
  auto second = q.pop_next(20);
  REQUIRE(second.has_value());
  CHECK(std::holds_alternative<s::TrafficLightSeenEvent>(*second->event));

  q.push({audio::Priority::Nav, tl.text, 1000, audio::CueEvent{tl}});
  CHECK(!q.pop_next(4001).has_value());  // > 3 s old: dropped, never said late
}

MINI_TEST_MAIN()
