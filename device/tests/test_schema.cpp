// Event schema tests: every example in shared/examples/events.jsonl must parse into its generated
// C++ struct and round-trip to identical JSON; invalid payloads must be rejected.
#include <fstream>
#include <string>

#include "common/json.hpp"
#include "events.gen.hpp"
#include "mini_test.hpp"

using namespace brim;
using namespace brim::schema;

template <class T>
static bool roundtrip(const json::Value& j) {
  T t{};
  std::string err;
  if (!from_json(j, t, &err)) {
    std::printf("  from_json failed: %s\n", err.c_str());
    return false;
  }
  json::Value back = to_json(t);
  if (back != j) {
    std::printf("  roundtrip mismatch:\n    in : %s\n    out: %s\n", j.dump().c_str(), back.dump().c_str());
    return false;
  }
  return true;
}

TEST_CASE("json parser basics") {
  auto v = json::parse(R"({"a":[1,2.5,"x\né"],"b":true,"c":null,"d":-3})");
  REQUIRE(v.has_value());
  CHECK_EQ(v->find("a")->as_array().size(), 3u);
  CHECK_EQ(v->find("a")->as_array()[2].as_string(), "x\n\xc3\xa9");
  CHECK(v->get_bool("b"));
  CHECK(v->find("c")->is_null());
  CHECK_EQ(v->get_int("d"), -3);
  CHECK(!json::parse("{\"a\":}"));
  CHECK(!json::parse("[1,2"));
  CHECK(!json::parse("{} trailing"));
  auto again = json::parse(v->dump());
  REQUIRE(again.has_value());
  CHECK(*again == *v);
}

TEST_CASE("all example events parse and round-trip") {
  std::ifstream f(std::string(BRIM_REPO_ROOT) + "/shared/examples/events.jsonl");
  REQUIRE(f.good());
  std::string line;
  int n = 0;
  while (std::getline(f, line)) {
    if (line.empty()) continue;
    auto j = json::parse(line);
    REQUIRE(j.has_value());
    auto type = brimEvent_type_of(*j);
    REQUIRE(type.has_value());
    bool ok = false;
    switch (*type) {
      case BrimEventType::PersonSeen: ok = roundtrip<PersonSeenEvent>(*j); break;
      case BrimEventType::ObstacleSeen: ok = roundtrip<ObstacleSeenEvent>(*j); break;
      case BrimEventType::ClearPath: ok = roundtrip<ClearPathEvent>(*j); break;
      case BrimEventType::SafetyAlert: ok = roundtrip<SafetyAlertEvent>(*j); break;
      case BrimEventType::Fault: ok = roundtrip<FaultEvent>(*j); break;
      case BrimEventType::Health: ok = roundtrip<HealthEvent>(*j); break;
    }
    CHECK(ok);
    n++;
  }
  CHECK_EQ(n, 6);
}

TEST_CASE("missing required field is rejected") {
  auto j = json::parse(R"({"type":"person_seen","seq":1,"source":"brim","t_mono_ms":1,"track_id":1,"direction":"left","confidence":0.9})");
  REQUIRE(j.has_value());
  PersonSeenEvent e;
  std::string err;
  CHECK(!from_json(*j, e, &err));
  CHECK(err.find("proximity") != std::string::npos);
}

TEST_CASE("wrong discriminator and bad enum are rejected") {
  auto j = json::parse(
      R"({"type":"obstacle_seen","seq":1,"source":"brim","t_mono_ms":1,"label":"dragon","direction":"ahead","proximity":"close","confidence":0.9})");
  REQUIRE(j.has_value());
  ObstacleSeenEvent o;
  CHECK(!from_json(*j, o));  // "dragon" is not an ObstacleKind
  PersonSeenEvent p;
  CHECK(!from_json(*j, p));  // type mismatch
  CHECK(!brimEvent_type_of(*json::parse(R"({"type":"nope"})")).has_value());
  CHECK(!brimEvent_type_of(*json::parse(R"({"type":"person_recognized"})")).has_value());  // gone for good
}

TEST_CASE("clear_path says 'stop' rather than guessing a direction") {
  auto j = json::parse(
      R"({"type":"clear_path","seq":9,"source":"brim","t_mono_ms":9,"clear":false,"direction":"unknown","text":"Path unclear, stop","blocked":["left","ahead","right"]})");
  REQUIRE(j.has_value());
  ClearPathEvent e;
  REQUIRE(from_json(*j, e));
  CHECK(!e.clear);
  CHECK(e.direction == Direction::Unknown);
  REQUIRE(e.blocked.has_value());
  CHECK_EQ(e.blocked->size(), 3u);
  CHECK(to_json(e) == *j);
}

TEST_CASE("optional fields: absent stays absent, present is kept") {
  SafetyAlertEvent a;
  a.seq = 1; a.source = "brim"; a.t_mono_ms = 5;
  a.kind = SafetyKind::ObjectClose; a.severity = Severity::Warning; a.text = "Chair close ahead";
  a.direction = Direction::Ahead;
  auto j = to_json(a);
  CHECK(j.find("confidence") == nullptr);
  CHECK(j.find("distance_m") == nullptr);  // there is no distance sensor on the hat
  CHECK_EQ(j.get_string("kind"), "object_close");
  a.confidence = 0.8;
  SafetyAlertEvent b;
  REQUIRE(from_json(to_json(a), b));
  REQUIRE(b.confidence.has_value());
  CHECK(*b.confidence == 0.8);
}

TEST_CASE("API payloads: Settings") {
  Settings s{70, 1.0, true, false, Sensitivity::Normal, 10};
  Settings t;
  REQUIRE(from_json(to_json(s), t));
  CHECK_EQ(t.volume, 70);
  CHECK(t.announce_people);
  CHECK(!t.announce_obstacles);
  CHECK(t.safety_sensitivity == Sensitivity::Normal);
  auto bad = json::parse(R"({"volume":"loud"})");
  CHECK(!from_json(*bad, t));
}

TEST_CASE("commands carry their payload: settings and the pairing PIN") {
  Command c;
  c.t_mono_ms = 12;
  c.command = CommandKind::ReloadSettings;
  c.settings = Settings{55, 1.25, false, true, Sensitivity::High, 30};
  Command back;
  REQUIRE(from_json(to_json(c), back));
  REQUIRE(back.settings.has_value());
  CHECK_EQ(back.settings->volume, 55);
  CHECK(back.settings->safety_sensitivity == Sensitivity::High);
  CHECK(!back.pin.has_value());

  Command pin;
  pin.t_mono_ms = 13;
  pin.command = CommandKind::SpeakPairingPin;
  pin.pin = "048275";
  Command pin_back;
  REQUIRE(from_json(to_json(pin), pin_back));
  REQUIRE(pin_back.pin.has_value());
  CHECK_EQ(*pin_back.pin, "048275");
  CHECK(!pin_back.settings.has_value());
}

MINI_TEST_MAIN()
