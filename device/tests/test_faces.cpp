// Naming enrolled people, with made-up embeddings (no face model, no photo). Thresholds come from
// the shipped config/faces.json.
#include <cmath>
#include <cstdio>
#include <fstream>
#include <random>
#include <string>
#include <vector>

#include <sys/stat.h>
#include <unistd.h>

#include "common/config.hpp"
#include "detect/faces.hpp"
#include "mini_test.hpp"

using namespace brim;

namespace {

const config::FaceConfig& cfg() {
  static const auto c = config::load_faces(std::string(BRIM_REPO_ROOT) + "/config/faces.json");
  return c;
}

// A random 128-d "face"; near(v, k) is the same face seen again (small noise).
std::vector<float> face(unsigned seed) {
  std::mt19937 rng(seed);
  std::normal_distribution<float> n(0, 1);
  std::vector<float> v(128);
  for (auto& x : v) x = n(rng);
  return v;
}
std::vector<float> near(const std::vector<float>& v, unsigned seed, float noise = 0.25f) {
  std::mt19937 rng(seed);
  std::normal_distribution<float> n(0, noise);
  auto out = v;
  for (auto& x : out) x += n(rng);
  return out;
}

void write_person(const std::string& dir, const std::string& folder, const std::string& name, const std::string& model,
                  const std::vector<float>& e) {
  mkdir((dir + "/" + folder).c_str(), 0700);
  std::ofstream f(dir + "/" + folder + "/embeddings.json");
  f << "{\"name\":\"" << name << "\",\"model\":\"" << model << "\",\"dim\":" << e.size() << ",\"embeddings\":[[";
  for (std::size_t i = 0; i < e.size(); ++i) f << (i ? "," : "") << e[i];
  f << "]]}";
}

}  // namespace

TEST_CASE("config: the match threshold is read from config/faces.json, with a floor") {
  CHECK(cfg().match_threshold >= 0.3 && cfg().match_threshold <= 0.9);
  CHECK(cfg().min_margin >= 0);
  CHECK(cfg().check_interval_ms >= 500);
  CHECK_EQ(cfg().faces_dir, std::string("data/faces"));
  CHECK(config::load_faces("/nonexistent/faces.json").match_threshold >= 0.3);  // defaults are safe too
}

TEST_CASE("a match gives the enrolled name") {
  faces::Gallery g;
  const auto tiffany = face(1);
  g.add("Tiffany", tiffany);
  auto m = g.match(near(tiffany, 99), cfg().match_threshold, cfg().min_margin);
  CHECK_EQ(m.name, std::string("Tiffany"));
  CHECK(m.score > cfg().match_threshold);
}

TEST_CASE("anyone not enrolled is unknown, never a guess") {
  faces::Gallery g;
  g.add("Tiffany", face(1));
  g.add("Kim", face(2));
  auto stranger = g.match(face(3), cfg().match_threshold, cfg().min_margin);
  CHECK(stranger.name.empty());
  CHECK(stranger.score < cfg().match_threshold);
  // A blur of two enrolled people (ambiguous) is not named either: the margin rule.
  auto a = face(1), b = face(2);
  auto unit = [](std::vector<float>& v) {
    double n = 0;
    for (float x : v) n += double(x) * x;
    for (float& x : v) x = static_cast<float>(x / std::sqrt(n));
  };
  unit(a), unit(b);
  std::vector<float> mix(128);
  for (int i = 0; i < 128; ++i) mix[i] = a[i] + b[i];  // equally like both: ambiguous
  auto m = g.match(mix, 0.3, cfg().min_margin);
  CHECK(m.name.empty());
  // Wrong size or all zeros: unknown.
  CHECK(g.match(std::vector<float>(64, 1.0f), cfg().match_threshold, cfg().min_margin).name.empty());
  CHECK(g.match(std::vector<float>(128, 0.0f), cfg().match_threshold, cfg().min_margin).name.empty());
  // Nobody enrolled: unknown.
  CHECK(faces::Gallery{}.match(face(1), cfg().match_threshold, cfg().min_margin).name.empty());
}

TEST_CASE("only 'Person ahead, ...' is ever named, in the allowed forms") {
  CHECK_EQ(faces::named_text("Person ahead, move left", "Tiffany"), std::string("Tiffany ahead, move left"));
  CHECK_EQ(faces::named_text("Person ahead, move right", "Tiffany"), std::string("Tiffany ahead, move right"));
  CHECK_EQ(faces::named_text("Person ahead, stop", "Tiffany"), std::string("Tiffany ahead, stop"));
  CHECK_EQ(faces::named_text("Chair ahead, move left", "Tiffany"), std::string("Chair ahead, move left"));
  CHECK_EQ(faces::named_text("Path unclear, stop", "Tiffany"), std::string("Path unclear, stop"));
  CHECK_EQ(faces::named_text("Person ahead, move left", "../etc"), std::string("Person ahead, move left"));
  CHECK_EQ(faces::named_text("Person ahead, move left", ""), std::string("Person ahead, move left"));
}

TEST_CASE("names must be plain names") {
  for (const char* ok : {"Tiffany", "Mary Jane", "O'Neil", "Jean-Luc"}) CHECK(faces::valid_name(ok));
  for (const char* bad : {"", "../x", "a/b", "1Tiffany", " Tiffany", "Tiffany\n", "abcdefghijklmnopqrstuvwxyzabcdefg"})
    CHECK(!faces::valid_name(bad));
}

TEST_CASE("the gallery loads enrolled folders only, from the same model, and skips anything odd") {
  char tmpl[] = "/tmp/brim_faces_XXXXXX";
  REQUIRE(mkdtemp(tmpl) != nullptr);
  const std::string dir = tmpl;
  const auto tiffany = face(1);
  write_person(dir, "Tiffany", "Tiffany", cfg().embedder_id, tiffany);
  write_person(dir, "Kim", "Kim", "some_other_model", face(2));      // another model: never used
  write_person(dir, "Bad1", "Someone Else", cfg().embedder_id, face(4));  // folder and name disagree
  write_person(dir, "x..y", "x..y", cfg().embedder_id, face(5));     // not a valid name
  auto g = faces::Gallery::load(dir, cfg().embedder_id);
  CHECK_EQ(g.people(), 1u);
  CHECK(g.has("Tiffany"));
  CHECK(!g.has("Kim"));
  CHECK_EQ(g.match(near(tiffany, 7), cfg().match_threshold, cfg().min_margin).name, std::string("Tiffany"));
  CHECK(faces::Gallery::load(dir + "/missing", cfg().embedder_id).empty());  // no folder: nobody
  std::string cmd = "rm -rf '" + dir + "'";
  CHECK_EQ(std::system(cmd.c_str()), 0);
}

TEST_CASE("names are forgotten quickly, and removed people are never said again") {
  faces::NameCache cache(4000);
  cache.set(7, "Tiffany", 1000);
  CHECK_EQ(cache.get(7, 3000), std::string("Tiffany"));
  CHECK(cache.get(7, 5001).empty());  // past the ttl
  CHECK(cache.get(8, 3000).empty());  // another track: unknown
  cache.set(7, "Tiffany", 6000);
  cache.set(9, "Kim", 6000);
  faces::Gallery only_kim;
  only_kim.add("Kim", face(2));
  cache.keep_only(only_kim);  // Tiffany was removed
  CHECK(cache.get(7, 6500).empty());
  CHECK_EQ(cache.get(9, 6500), std::string("Kim"));
  cache.forget(9);
  CHECK(cache.get(9, 6500).empty());
  cache.set(-1, "Kim", 0);  // untracked boxes are never named
  CHECK_EQ(cache.size(), 0u);
}

TEST_CASE("nobody enrolled: the recogniser is off and never names anyone") {
  config::FaceConfig c = cfg();
  c.faces_dir = "/nonexistent/faces";
  faces::Recogniser r(c, faces::make_face_embedder(c, "models"));
  r.start();
  if (faces::opencv_available()) CHECK_EQ(r.status(), std::string("off (nobody enrolled)"));
  else CHECK(r.status().rfind("unavailable", 0) == 0);  // laptop build: says so, never pretends
  capture::Frame f;
  f.width = 4, f.height = 4;
  f.rgb.assign(4 * 4 * 3, 0);
  detect::Detection d;
  d.thing = detect::Thing::Person;
  d.track_id = 3;
  r.offer(f, d, 1000);  // ignored: no model runs
  CHECK(r.name_for(3, 1000).empty());
  r.stop();
}

MINI_TEST_MAIN()
