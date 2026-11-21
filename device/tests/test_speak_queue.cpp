#include "audio/speak_queue.hpp"
#include "mini_test.hpp"

using namespace brim::audio;

static SpeechItem item(Priority p, const char* text, std::int64_t t) { return SpeechItem{p, text, t, std::nullopt}; }

TEST_CASE("priority order: safety > nav > info") {
  SpeakQueue q;
  q.push(item(Priority::Info, "Person on your left", 0));
  q.push(item(Priority::Nav, "Move right", 0));
  q.push(item(Priority::Safety, "Person ahead, move right", 0));
  CHECK_EQ(q.pop_next(100)->text, "Person ahead, move right");
  CHECK_EQ(q.pop_next(100)->text, "Move right");
  CHECK_EQ(q.pop_next(100)->text, "Person on your left");
  CHECK(!q.pop_next(100));
}

TEST_CASE("a direction is never spoken before the warning that caused it") {
  SpeakQueue q;
  q.push(item(Priority::Nav, "Move right", 0));            // queued first...
  q.push(item(Priority::Safety, "Chair ahead, move left", 1));  // ...but safety still goes first
  CHECK_EQ(q.pop_next(10)->text, "Chair ahead, move left");
  CHECK_EQ(q.pop_next(10)->text, "Move right");
}

TEST_CASE("FIFO within a priority") {
  SpeakQueue q;
  q.push(item(Priority::Nav, "Move right", 0));
  q.push(item(Priority::Nav, "Clear path ahead", 1));
  CHECK_EQ(q.pop_next(10)->text, "Move right");
  CHECK_EQ(q.pop_next(10)->text, "Clear path ahead");
}

TEST_CASE("cues carry their event, to be stamped when sent") {
  SpeakQueue q;
  brim::schema::SafetyAlertEvent e;
  e.text = "Person ahead, move right";
  q.push(SpeechItem{Priority::Safety, e.text, 0, CueEvent{e}});
  auto it = q.pop_next(5);
  REQUIRE(it.has_value());
  REQUIRE(it->event.has_value());
  REQUIRE(std::holds_alternative<brim::schema::SafetyAlertEvent>(*it->event));
  CHECK_EQ(std::get<brim::schema::SafetyAlertEvent>(*it->event).text, "Person ahead, move right");
}

TEST_CASE("safety is never dropped for age; a stale direction is") {
  SpeakQueue q;
  q.push(item(Priority::Safety, "Person ahead, move right", 0));
  q.push(item(Priority::Nav, "Move right", 0));
  CHECK_EQ(q.pop_next(5000)->text, "Person ahead, move right");  // 5 s old, still sent
  CHECK(!q.pop_next(5000));                                // "Move right" 5 s late: dropped
  CHECK_EQ(q.dropped_stale(), 1u);
}

TEST_CASE("info older than 3 s is dropped") {
  SpeakQueue q;
  q.push(item(Priority::Info, "Chair ahead", 1000));
  CHECK(!q.pop_next(4001));  // 3001 ms old
  CHECK_EQ(q.size(), 0u);
  CHECK_EQ(q.dropped_stale(), 1u);
}

TEST_CASE("info exactly 3 s old is still spoken") {
  SpeakQueue q;
  q.push(item(Priority::Info, "Chair ahead", 1000));
  CHECK(q.pop_next(4000).has_value());
}

TEST_CASE("at most one info announcement per 10 s") {
  SpeakQueue q;
  q.push(item(Priority::Info, "a", 0));
  CHECK(q.pop_next(0).has_value());
  q.push(item(Priority::Info, "b", 5000));
  CHECK(!q.pop_next(5000));   // only 5 s since the last one
  CHECK(!q.pop_next(9999));   // still rate-limited (and b now 4999 ms old -> dropped)
  q.push(item(Priority::Info, "c", 9500));
  auto it = q.pop_next(10000);
  REQUIRE(it.has_value());
  CHECK_EQ(it->text, "c");
}

TEST_CASE("the info rate limit never delays nav or safety") {
  SpeakQueue q;
  q.push(item(Priority::Info, "a", 0));
  CHECK(q.pop_next(0).has_value());
  q.push(item(Priority::Nav, "Move left", 100));
  q.push(item(Priority::Safety, "Chair ahead, move left", 100));
  CHECK_EQ(q.pop_next(200)->text, "Chair ahead, move left");
  CHECK_EQ(q.pop_next(200)->text, "Move left");
}

TEST_CASE("settings change the info interval at runtime") {
  SpeakQueue q;
  q.push(item(Priority::Info, "a", 0));
  CHECK(q.pop_next(0).has_value());
  SpeakQueueConfig cfg;
  cfg.info_min_interval_ms = 30000;  // wearer asked for a quieter hat
  q.set_config(cfg);
  q.push(item(Priority::Info, "b", 20000));
  CHECK(!q.pop_next(20000));
  q.push(item(Priority::Info, "c", 30000));
  CHECK_EQ(q.pop_next(30000)->text, "c");
}

TEST_CASE("capacity: evicts lowest priority, never safety for an info item") {
  SpeakQueueConfig cfg;
  cfg.capacity = 2;
  SpeakQueue q(cfg);
  CHECK(q.push(item(Priority::Safety, "s1", 0)));
  CHECK(q.push(item(Priority::Safety, "s2", 0)));
  CHECK(!q.push(item(Priority::Info, "i", 0)));      // rejected: would need to evict safety
  CHECK(q.push(item(Priority::Safety, "s3", 0)));     // evicts oldest safety
  CHECK_EQ(q.size(), 2u);
  CHECK_EQ(q.pop_next(0)->text, "s2");

  SpeakQueue q2(cfg);
  q2.push(item(Priority::Info, "i1", 0));
  q2.push(item(Priority::Nav, "n1", 0));
  CHECK(q2.push(item(Priority::Safety, "s", 0)));    // evicts the info item
  CHECK_EQ(q2.evicted(), 1u);
  CHECK_EQ(q2.pop_next(0)->text, "s");
  CHECK_EQ(q2.pop_next(0)->text, "n1");
}

MINI_TEST_MAIN()
