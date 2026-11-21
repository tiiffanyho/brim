// SQLite layer tests: migrations, anonymous sightings, the 30-minute privacy purge, alerts,
// settings defaults/round-trip, sessions. There are no people in the database any more.
#include <sys/stat.h>
#include <unistd.h>

#include <cstdlib>
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
    CHECK(db->schema_version() >= 2);
    CHECK(db->ping());
  }
  auto again = fresh(dir);  // reopening must not re-apply
  REQUIRE(again);
  CHECK(again->schema_version() >= 2);
}

TEST_CASE("no people table survives the identity removal") {
  auto dir = temp_dir();
  auto db = fresh(dir);
  REQUIRE(db);
  // A query against a dropped table fails to prepare, so activity() is the only reader left and
  // it returns nothing but anonymous rows.
  CHECK_EQ(db->activity(10).size(), 0u);
}

TEST_CASE("sightings: anonymous people and named obstacles, newest first") {
  auto dir = temp_dir();
  auto db = fresh(dir);
  REQUIRE(db);
  const std::int64_t t0 = 1'700'000'000'000;
  db->record_person_seen(7, "left", "close", 0.93, 10, t0);
  db->record_obstacle_seen("chair", 8, "ahead", "near", 0.71, 20, t0 + 1000);

  auto a = db->activity(10);
  REQUIRE(a.size() == 2);
  CHECK(a[0].kind == schema::ActivityKind::Obstacle);
  REQUIRE(a[0].label.has_value());
  CHECK(*a[0].label == schema::ObstacleKind::Chair);
  CHECK(a[0].direction == schema::Direction::Ahead);
  CHECK(a[0].proximity == schema::Proximity::Near);
  CHECK(a[1].kind == schema::ActivityKind::Person);
  CHECK(!a[1].label.has_value());          // people are never labelled
  REQUIRE(a[1].track_id.has_value());
  CHECK_EQ(*a[1].track_id, 7);
  CHECK(a[1].proximity == schema::Proximity::Close);
}

TEST_CASE("an obstacle we could not name is stored as 'obstacle'") {
  auto dir = temp_dir();
  auto db = fresh(dir);
  REQUIRE(db);
  db->record_obstacle_seen("obstacle", 0, "right", "close", 0.64, 1);
  auto a = db->activity(10);
  REQUIRE(a.size() == 1);
  REQUIRE(a[0].label.has_value());
  CHECK(*a[0].label == schema::ObstacleKind::Obstacle);
  CHECK(!a[0].track_id.has_value());  // no track: not stored as one
}

TEST_CASE("every sighting is purged 30 minutes after it happened") {
  auto dir = temp_dir();
  auto db = fresh(dir);
  REQUIRE(db);
  const std::int64_t t0 = 1'700'000'000'000;
  db->record_person_seen(1, "ahead", "close", 0.9, 1, t0);
  db->record_obstacle_seen("pole", 2, "left", "near", 0.8, 2, t0);
  db->record_person_seen(3, "right", "near", 0.9, 3, t0 + 20 * 60 * 1000);
  CHECK_EQ(db->activity(10).size(), 3u);

  db->purge(t0 + 29 * 60 * 1000);
  CHECK_EQ(db->activity(10).size(), 3u);
  db->purge(t0 + 30 * 60 * 1000);
  auto a = db->activity(10);
  REQUIRE(a.size() == 1);
  CHECK_EQ(*a[0].track_id, 3);
  db->purge(t0 + 50 * 60 * 1000 + 1);
  CHECK_EQ(db->activity(10).size(), 0u);
}

TEST_CASE("alerts stored newest first") {
  auto dir = temp_dir();
  auto db = fresh(dir);
  REQUIRE(db);
  schema::SafetyAlertEvent e;
  e.kind = schema::SafetyKind::ObjectClose;
  e.severity = schema::Severity::Warning;
  e.text = "Chair close ahead";
  e.direction = schema::Direction::Ahead;
  db->add_alert(e, 1000);
  e.kind = schema::SafetyKind::PersonClose;
  e.text = "Person close ahead";
  e.severity = schema::Severity::Critical;
  db->add_alert(e, 2000);
  auto a = db->alerts(10);
  REQUIRE(a.size() == 2);
  CHECK_EQ(a[0].text, "Person close ahead");
  CHECK(a[0].kind == schema::SafetyKind::PersonClose);
  CHECK(a[0].severity == schema::Severity::Critical);
}

TEST_CASE("settings default, round-trip, partial rows") {
  auto dir = temp_dir();
  auto db = fresh(dir);
  REQUIRE(db);
  auto s = db->get_settings();
  CHECK_EQ(s.volume, 80);
  CHECK_EQ(s.info_min_interval_s, 10);
  CHECK(s.announce_people);
  CHECK(s.announce_obstacles);
  s.volume = 35;
  s.announce_people = false;
  s.safety_sensitivity = schema::Sensitivity::High;
  CHECK(db->put_settings(s));
  auto t = db->get_settings();
  CHECK_EQ(t.volume, 35);
  CHECK(!t.announce_people);
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
