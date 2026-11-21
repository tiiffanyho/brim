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
      case BrimEventType::PersonRecognized: ok = roundtrip<PersonRecognizedEvent>(*j); break;
      case BrimEventType::UnknownPerson: ok = roundtrip<UnknownPersonEvent>(*j); break;
      case BrimEventType::Cue: ok = roundtrip<CueEvent>(*j); break;
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
  auto j = json::parse(R"({"type":"person_recognized","seq":1,"source":"brim","t_mono_ms":1,"person_id":1,"confidence":0.9,"direction":"left"})");
  REQUIRE(j.has_value());
  PersonRecognizedEvent e;
  std::string err;
  CHECK(!from_json(*j, e, &err));
  CHECK(err.find("display_name") != std::string::npos);
}

TEST_CASE("wrong discriminator and bad enum are rejected") {
  auto j = json::parse(R"({"type":"cue","seq":1,"source":"brim","t_mono_ms":1,"cue":"dancing","text":"x"})");
  REQUIRE(j.has_value());
  CueEvent c;
  CHECK(!from_json(*j, c));  // "dancing" is not a CueKind
  PersonRecognizedEvent p;
  CHECK(!from_json(*j, p));  // type mismatch
  CHECK(!brimEvent_type_of(*json::parse(R"({"type":"nope"})")).has_value());
}

TEST_CASE("optional fields: absent stays absent, present is kept") {
  SafetyAlertEvent a;
  a.seq = 1; a.source = "brim"; a.t_mono_ms = 5;
  a.kind = SafetyKind::StepOrCurb; a.severity = Severity::Warning; a.text = "Curb"; a.direction = Direction::Ahead;
  auto j = to_json(a);
  CHECK(j.find("distance_m") == nullptr);
  CHECK_EQ(j.get_string("kind"), "step_or_curb");
  a.distance_m = 0.8;
  SafetyAlertEvent b;
  REQUIRE(from_json(to_json(a), b));
  REQUIRE(b.distance_m.has_value());
  CHECK(*b.distance_m == 0.8);
}

TEST_CASE("API payloads: Settings and Person") {
  Settings s{70, 1.0, true, false, Sensitivity::Normal, 10};
  Settings t;
  REQUIRE(from_json(to_json(s), t));
  CHECK_EQ(t.volume, 70);
  CHECK(t.safety_sensitivity == Sensitivity::Normal);
  auto bad = json::parse(R"({"volume":"loud"})");
  CHECK(!from_json(*bad, t));
}

MINI_TEST_MAIN()
