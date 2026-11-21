// Privacy rules in the recognition layer: strangers are anonymous and forgotten after 30 minutes,
// "forget" removes an enrolled person immediately, names are not repeated every frame.
#include "cues/cue_engine.hpp"
#include "mini_test.hpp"
#include "recognize/recognizer.hpp"

using namespace brim;
using recognize::Embedding;

static Embedding unit(int axis) {
  Embedding e{};
  e[axis] = 1.0f;
  return e;
}

TEST_CASE("stranger keeps the same anon id while seen, forgotten after 30 min") {
  recognize::StrangerTracker t(30 * 60 * 1000);
  auto a = t.observe(unit(0), 0);
  CHECK_EQ(t.observe(unit(0), 60 * 1000), a);          // same face a minute later
  CHECK(t.observe(unit(1), 60 * 1000) != a);           // different face -> different id
  CHECK_EQ(t.size(), 2u);
  t.expire(60 * 1000 + 30 * 60 * 1000);                // 30 min after last sighting
  CHECK_EQ(t.size(), 0u);
  auto again = t.observe(unit(0), 2 * 60 * 60 * 1000);
  CHECK(again != a);                                   // no memory of the earlier stranger
}

TEST_CASE("stranger ids never contain a name") {
  recognize::StrangerTracker t;
  CHECK_EQ(t.observe(unit(3), 0).rfind("stranger-", 0), 0u);
}

TEST_CASE("gallery: match, rename, forget") {
  recognize::Gallery g;
  g.add(1, "Sam", unit(0));
  g.add(2, "Priya", unit(1));
  auto m = g.best_match(unit(0), 0.8f);
  REQUIRE(m.has_value());
  CHECK_EQ(m->person_id, 1);
  CHECK_EQ(m->display_name, "Sam");
  CHECK(!g.best_match(unit(5), 0.8f).has_value());     // unknown face -> no name
  g.rename(1, "Samuel");
  CHECK_EQ(g.best_match(unit(0), 0.8f)->display_name, "Samuel");
  CHECK(g.forget(1));
  CHECK(!g.best_match(unit(0), 0.8f).has_value());
  CHECK_EQ(g.size(), 1u);
}

TEST_CASE("name announcer: once per repeat window") {
  cues::NameAnnouncer n(120000);
  CHECK(n.should_announce(1, 0));
  CHECK(!n.should_announce(1, 1000));
  CHECK(n.should_announce(2, 1000));
  CHECK(n.should_announce(1, 121000));
  n.forget(2);
  CHECK(n.should_announce(2, 2000));
}

MINI_TEST_MAIN()
