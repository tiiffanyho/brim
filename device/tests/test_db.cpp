// SQLite layer tests: migrations, people CRUD, DELETE removes files (and only inside data_dir),
// stranger sightings purged after 30 minutes, settings defaults/round-trip, sessions.
#include <sys/stat.h>
#include <unistd.h>

#include <cstdlib>
#include <fstream>
#include <string>

#include "app_server/db.hpp"
#include "mini_test.hpp"

using namespace brim;
using namespace brim::app;

static const std::string kMigrations = std::string(BRIM_REPO_ROOT) + "/data/migrations";

static std::string temp_dir() {
  char tmpl[] = "/tmp/brim_test_XXXXXX";
  return mkdtemp(tmpl);
}
static bool exists(const std::string& p) {
  struct stat st;
  return ::stat(p.c_str(), &st) == 0;
}
static std::unique_ptr<Db> fresh(const std::string& dir) {
  std::string err;
  auto db = Db::open(dir + "/brim.db", kMigrations, &err);
  if (!db) std::printf("  open failed: %s\n", err.c_str());
  return db;
}

TEST_CASE("migrations apply and are idempotent") {
  auto dir = temp_dir();
  {
    auto db = fresh(dir);
    REQUIRE(db);
    CHECK(db->schema_version() >= 1);
    CHECK(db->ping());
  }
  auto again = fresh(dir);  // reopening must not re-apply
  REQUIRE(again);
  CHECK(again->schema_version() >= 1);
}

TEST_CASE("people: add, list, rename") {
  auto dir = temp_dir();
  auto db = fresh(dir);
  REQUIRE(db);
  auto id = db->add_person("Sam", "clips/1.wav", "embeddings/1.bin");
  CHECK(id > 0);
  db->add_person("alex", "", "");
  auto people = db->list_people();
  REQUIRE(people.size() == 2);
  CHECK_EQ(people[0].display_name, "alex");  // case-insensitive sort
  CHECK(people[1].has_embedding && people[1].has_name_clip);
  CHECK(!people[0].has_embedding);
  auto renamed = db->rename_person(id, "Samuel");
  REQUIRE(renamed.has_value());
  CHECK_EQ(renamed->display_name, "Samuel");
  CHECK(!db->rename_person(999, "x").has_value());
}

TEST_CASE("delete person removes their files and sightings") {
  auto dir = temp_dir();
  auto db = fresh(dir);
  REQUIRE(db);
  db->seed_demo(dir);
  CHECK(exists(dir + "/embeddings/1.bin"));
  CHECK(exists(dir + "/clips/1.wav"));
  db->record_recognized(1, "left", 0.9, 100);
  CHECK_EQ(db->activity(10).size(), 1u);

  CHECK(db->delete_person(1, dir) == Db::DeleteResult::Deleted);
  CHECK(!exists(dir + "/embeddings/1.bin"));
  CHECK(!exists(dir + "/clips/1.wav"));
  CHECK(exists(dir + "/embeddings/2.bin"));       // others untouched
  CHECK(!db->get_person(1).has_value());
  CHECK_EQ(db->activity(10).size(), 0u);          // sightings cascade
  CHECK(db->delete_person(1, dir) == Db::DeleteResult::NotFound);
}

TEST_CASE("delete refuses paths outside the data dir") {
  auto dir = temp_dir();
  auto outside = temp_dir() + "/secret.txt";
  std::ofstream(outside) << "x";
  auto db = fresh(dir);
  REQUIRE(db);
  auto id = db->add_person("Mallory", "", "../" + outside.substr(5));
  std::string err;
  CHECK(db->delete_person(id, dir, &err) == Db::DeleteResult::FileError);
  CHECK(exists(outside));
  CHECK(db->get_person(id).has_value());  // not reported as forgotten
  auto id2 = db->add_person("Abs", outside, "");
  CHECK(db->delete_person(id2, dir, &err) == Db::DeleteResult::FileError);
  CHECK(exists(outside));
}

TEST_CASE("strangers purged after 30 minutes, recognized kept") {
  auto dir = temp_dir();
  auto db = fresh(dir);
  REQUIRE(db);
  db->seed_demo(dir);
  const std::int64_t t0 = 1'700'000'000'000;
  db->record_stranger("stranger-1", "ahead", 1, t0);
  db->record_recognized(2, "right", 0.95, 2, t0);
  db->record_stranger("stranger-2", "left", 3, t0 + 20 * 60 * 1000);
  CHECK_EQ(db->activity(10).size(), 3u);

  db->purge(t0 + 29 * 60 * 1000);
  CHECK_EQ(db->activity(10).size(), 3u);
  db->purge(t0 + 30 * 60 * 1000);
  auto a = db->activity(10);
  REQUIRE(a.size() == 2);
  CHECK(a[0].anon_id.has_value() && *a[0].anon_id == "stranger-2");
  CHECK(a[1].kind == schema::ActivityKind::Recognized);
  CHECK(a[1].display_name.has_value() && *a[1].display_name == "Priya");
  CHECK(!a[1].anon_id.has_value());
}

TEST_CASE("record_recognized ignores unknown person ids and sets last_seen") {
  auto dir = temp_dir();
  auto db = fresh(dir);
  REQUIRE(db);
  db->seed_demo(dir);
  db->record_recognized(42, "left", 0.9, 1);
  CHECK_EQ(db->activity(10).size(), 0u);
  db->record_recognized(3, "ahead", 0.9, 1);
  CHECK(db->get_person(3)->last_seen_at.has_value());
}

TEST_CASE("alerts stored newest first") {
  auto dir = temp_dir();
  auto db = fresh(dir);
  REQUIRE(db);
  schema::SafetyAlertEvent e;
  e.kind = schema::SafetyKind::StepOrCurb;
  e.severity = schema::Severity::Warning;
  e.text = "Step down";
  e.direction = schema::Direction::Ahead;
  db->add_alert(e, 1000);
  e.text = "Branch";
  e.severity = schema::Severity::Critical;
  db->add_alert(e, 2000);
  auto a = db->alerts(10);
  REQUIRE(a.size() == 2);
  CHECK_EQ(a[0].text, "Branch");
  CHECK(a[0].severity == schema::Severity::Critical);
}

TEST_CASE("settings default, round-trip, partial rows") {
  auto dir = temp_dir();
  auto db = fresh(dir);
  REQUIRE(db);
  auto s = db->get_settings();
  CHECK_EQ(s.volume, 80);
  CHECK_EQ(s.cue_min_interval_s, 10);
  s.volume = 35;
  s.announce_strangers = true;
  s.safety_sensitivity = schema::Sensitivity::High;
  CHECK(db->put_settings(s));
  auto t = db->get_settings();
  CHECK_EQ(t.volume, 35);
  CHECK(t.announce_strangers);
  CHECK(t.safety_sensitivity == schema::Sensitivity::High);
}

TEST_CASE("sessions") {
  auto dir = temp_dir();
  auto db = fresh(dir);
  REQUIRE(db);
  CHECK(!db->check_session("abc"));
  CHECK(!db->check_session(""));
  db->add_session("abc");
  CHECK(db->check_session("abc"));
}

MINI_TEST_MAIN()
