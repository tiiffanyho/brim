// Privacy rules, enforced in code so they cannot be lost by accident:
//   * nobody is identified — a person is a box with a throw-away track id;
//   * a track id says "the same person as a moment ago" and nothing more: once a track is
//     forgotten, the same person coming back is a brand new, unrelated id;
//   * every track is cleared within 30 minutes;
//   * the event schema itself has no identity fields at all.
#include <fstream>
#include <sstream>
#include <string>

#include "common/config.hpp"
#include "common/json.hpp"
#include "detect/detector.hpp"
#include "events.gen.hpp"
#include "mini_test.hpp"

using namespace brim;
using brim::detect::Box;
using brim::detect::Detection;
using brim::detect::Thing;

static Detection det(Thing t, float x, float y, float w = 0.1f, float h = 0.3f, float score = 0.9f) {
  Detection d;
  d.thing = t;
  d.box = Box{x, y, w, h};
  d.score = score;
  return d;
}

TEST_CASE("a person keeps one track id while in view, and a second person gets another") {
  detect::TrackStore tracks;
  std::vector<Detection> f1{det(Thing::Person, 0.10f, 0.2f)};
  tracks.observe(f1, 0);
  const int id = f1[0].track_id;
  CHECK(id > 0);

  std::vector<Detection> f2{det(Thing::Person, 0.12f, 0.2f), det(Thing::Person, 0.70f, 0.2f)};
  tracks.observe(f2, 100);
  CHECK_EQ(f2[0].track_id, id);        // drifted a little: same person
  CHECK(f2[1].track_id != id);         // someone else entirely
  CHECK_EQ(tracks.size(), 2u);
}

TEST_CASE("someone walking across the frame keeps one track id") {
  detect::TrackStore tracks;
  int id = -1;
  // 7.5 detections/s (every 2nd frame), crossing left to right in ~2 s, with one missed detection.
  for (int k = 0; k <= 15; ++k) {
    if (k == 8) continue;  // detector missed them on this frame
    std::vector<Detection> f{det(Thing::Person, 0.05f + 0.05f * k, 0.2f, 0.12f, 0.6f)};
    tracks.observe(f, k * 133);
    if (id < 0) id = f[0].track_id;
    CHECK_EQ(f[0].track_id, id);
  }
  CHECK_EQ(tracks.size(), 1u);
}

TEST_CASE("two people crossing paths keep their own ids") {
  detect::TrackStore tracks;
  std::vector<Detection> f0{det(Thing::Person, 0.20f, 0.2f), det(Thing::Person, 0.60f, 0.2f)};
  tracks.observe(f0, 0);
  const int a = f0[0].track_id, b = f0[1].track_id;
  // Next frame, listed in the other order and each moved a little towards the other.
  std::vector<Detection> f1{det(Thing::Person, 0.56f, 0.2f), det(Thing::Person, 0.24f, 0.2f)};
  tracks.observe(f1, 100);
  CHECK_EQ(f1[0].track_id, b);
  CHECK_EQ(f1[1].track_id, a);
}

TEST_CASE("a track gone for longer than the match window is not handed to someone new") {
  detect::TrackStore tracks(30 * 60 * 1000, 10000, 0.3f, 1500);
  std::vector<Detection> f0{det(Thing::Person, 0.40f, 0.2f)};
  tracks.observe(f0, 0);
  std::vector<Detection> f1{det(Thing::Person, 0.40f, 0.2f)};
  tracks.observe(f1, 5000);  // same place, 5 s later: could be anyone
  CHECK(f1[0].track_id != f0[0].track_id);
  CHECK_EQ(tracks.active(5000), 1u);
}

TEST_CASE("a chair never takes over a person's track") {
  detect::TrackStore tracks;
  std::vector<Detection> f0{det(Thing::Person, 0.40f, 0.2f)};
  tracks.observe(f0, 0);
  std::vector<Detection> f1{det(Thing::Chair, 0.40f, 0.2f)};
  tracks.observe(f1, 100);
  CHECK(f1[0].track_id != f0[0].track_id);
}

TEST_CASE("tracks are cleared after 30 minutes and the person is not recognized again") {
  detect::TrackStore tracks(30 * 60 * 1000);
  std::vector<Detection> f1{det(Thing::Person, 0.10f, 0.2f)};
  tracks.observe(f1, 0);
  const int id = f1[0].track_id;

  tracks.expire(29 * 60 * 1000);
  CHECK_EQ(tracks.size(), 1u);
  tracks.expire(30 * 60 * 1000);
  CHECK_EQ(tracks.size(), 0u);

  std::vector<Detection> later{det(Thing::Person, 0.10f, 0.2f)};  // the very same person, hours later
  tracks.observe(later, 3 * 60 * 60 * 1000);
  CHECK(later[0].track_id != id);  // no memory of them: a new, meaningless id
}

TEST_CASE("clear() forgets everything immediately") {
  detect::TrackStore tracks;
  std::vector<Detection> f{det(Thing::Person, 0.1f, 0.2f), det(Thing::Chair, 0.5f, 0.5f)};
  tracks.observe(f, 0);
  CHECK_EQ(tracks.size(), 2u);
  tracks.clear();
  CHECK_EQ(tracks.size(), 0u);
}

TEST_CASE("the same track is announced at most once per suppression window") {
  detect::TrackStore tracks(30 * 60 * 1000, 10000);
  std::vector<Detection> f{det(Thing::Person, 0.1f, 0.2f)};
  tracks.observe(f, 0);
  const int id = f[0].track_id;
  CHECK(tracks.should_announce(id, 0));
  CHECK(!tracks.should_announce(id, 5000));
  CHECK(tracks.should_announce(id, 10000));
  CHECK(!tracks.should_announce(999, 10000));  // unknown track: never announced
}

TEST_CASE("person events carry no identity") {
  schema::PersonSeenEvent e;
  e.seq = 1;
  e.source = "brim";
  e.t_mono_ms = 1;
  e.track_id = 4;
  e.direction = schema::Direction::Left;
  e.proximity = schema::Proximity::Close;
  e.confidence = 0.9;
  auto j = schema::to_json(e);
  for (const char* forbidden : {"display_name", "person_id", "name", "anon_id", "embedding"})
    CHECK(j.find(forbidden) == nullptr);
  CHECK_EQ(j.get_int("track_id"), 4);
}

TEST_CASE("the event schema has no face data, and names only where the team agreed") {
  std::ifstream f(std::string(BRIM_REPO_ROOT) + "/shared/events.schema.json");
  REQUIRE(f.good());
  std::stringstream ss;
  ss << f.rdbuf();
  const std::string schema = ss.str();
  // Quoted so that prose in the descriptions ("no face data") does not trip this.
  for (const char* forbidden : {"\"display_name\"", "\"person_id\"", "\"embedding", "\"anon_id\"", "\"name_clip", "\"cue\""}) {
    if (schema.find(forbidden) != std::string::npos) std::printf("  schema still mentions %s\n", forbidden);
    CHECK(schema.find(forbidden) == std::string::npos);
  }
  // Face recognition of enrolled people (agreed by the team): an optional "name" on safety_alert
  // and clear_path only. person_seen and everything stored (Alert, ActivityItem) stay anonymous.
  auto j = json::parse(schema);
  REQUIRE(j.has_value());
  const json::Value* defs = j->find("$defs");
  REQUIRE(defs != nullptr);
  for (const char* def : {"SafetyAlertEvent", "ClearPathEvent"})
    CHECK(defs->find(def)->find("properties")->find("name") != nullptr);
  for (const char* def : {"PersonSeenEvent", "ObstacleSeenEvent", "Alert", "ActivityItem", "HealthEvent"})
    CHECK(defs->find(def)->find("properties")->find("name") == nullptr);
}

TEST_CASE("config floors: never chattier than 1 info per 10 s, never keep tracks past 30 min") {
  auto c = config::load_safety(std::string(BRIM_REPO_ROOT) + "/config/safety.json");
  CHECK(c.info_min_interval_s >= 10);
  CHECK(c.track_forget_s <= 1800);
  auto missing = config::load_safety("/nonexistent/safety.json");  // defaults must also be safe
  CHECK(missing.info_min_interval_s >= 10);
  CHECK(missing.track_forget_s <= 1800);
}

MINI_TEST_MAIN()
