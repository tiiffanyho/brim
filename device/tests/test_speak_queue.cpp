#include "audio/speak_queue.hpp"
#include "mini_test.hpp"

using namespace brim::audio;

static SpeechItem item(Priority p, const char* text, std::int64_t t) { return SpeechItem{p, text, "", t}; }

TEST_CASE("priority order: safety > nav > info") {
  SpeakQueue q;
  q.push(item(Priority::Info, "Person on your left", 0));
  q.push(item(Priority::Nav, "Step right", 0));
  q.push(item(Priority::Safety, "Person close ahead", 0));
  CHECK_EQ(q.pop_next(100, false)->text, "Person close ahead");
  CHECK_EQ(q.pop_next(100, false)->text, "Step right");
  CHECK_EQ(q.pop_next(100, false)->text, "Person on your left");
  CHECK(!q.pop_next(100, false));
}

TEST_CASE("a direction is never spoken before the warning that caused it") {
  SpeakQueue q;
  q.push(item(Priority::Nav, "Step right", 0));            // queued first...
  q.push(item(Priority::Safety, "Chair close ahead", 1));  // ...but safety still goes first
  CHECK_EQ(q.pop_next(10, false)->text, "Chair close ahead");
  CHECK_EQ(q.pop_next(10, false)->text, "Step right");
}

TEST_CASE("FIFO within a priority") {
  SpeakQueue q;
  q.push(item(Priority::Nav, "Step right", 0));
  q.push(item(Priority::Nav, "Clear path ahead", 1));
  CHECK_EQ(q.pop_next(10, false)->text, "Step right");
  CHECK_EQ(q.pop_next(10, false)->text, "Clear path ahead");
}

TEST_CASE("info speech waits for no-one-talking") {
  SpeakQueue q;
  q.push(item(Priority::Info, "Chair ahead", 1000));
  CHECK(!q.pop_next(1500, /*someone_talking=*/true));
  auto it = q.pop_next(1600, false);
  REQUIRE(it.has_value());
  CHECK_EQ(it->text, "Chair ahead");
}

TEST_CASE("safety and nav do not wait for silence") {
  SpeakQueue q;
  q.push(item(Priority::Safety, "Person close ahead", 0));
  q.push(item(Priority::Nav, "Path unclear, stop", 0));
  CHECK_EQ(q.pop_next(10, true)->text, "Person close ahead");
  CHECK_EQ(q.pop_next(10, true)->text, "Path unclear, stop");
}

TEST_CASE("info older than 3 s is dropped") {
  SpeakQueue q;
  q.push(item(Priority::Info, "Chair ahead", 1000));
  CHECK(!q.pop_next(4001, false));  // 3001 ms old
  CHECK_EQ(q.size(), 0u);
  CHECK_EQ(q.dropped_stale_info(), 1u);
}

TEST_CASE("info exactly 3 s old is still spoken") {
  SpeakQueue q;
  q.push(item(Priority::Info, "Chair ahead", 1000));
  CHECK(q.pop_next(4000, false).has_value());
}

TEST_CASE("stale info dropped while waiting for silence") {
  SpeakQueue q;
  q.push(item(Priority::Info, "Person on your left", 0));
  CHECK(!q.pop_next(1000, true));
  CHECK(!q.pop_next(2000, true));
  CHECK(!q.pop_next(3500, false));  // silence came too late
  CHECK_EQ(q.dropped_stale_info(), 1u);
}

TEST_CASE("at most one info announcement per 10 s") {
  SpeakQueue q;
  q.push(item(Priority::Info, "a", 0));
  CHECK(q.pop_next(0, false).has_value());
  q.push(item(Priority::Info, "b", 5000));
  CHECK(!q.pop_next(5000, false));   // only 5 s since the last one
  CHECK(!q.pop_next(9999, false));   // still rate-limited (and b now 4999 ms old -> dropped)
  q.push(item(Priority::Info, "c", 9500));
  auto it = q.pop_next(10000, false);
  REQUIRE(it.has_value());
  CHECK_EQ(it->text, "c");
}

TEST_CASE("the info rate limit never delays nav or safety") {
  SpeakQueue q;
  q.push(item(Priority::Info, "a", 0));
  CHECK(q.pop_next(0, false).has_value());
  q.push(item(Priority::Nav, "Step left", 100));
  q.push(item(Priority::Safety, "Chair close ahead", 100));
  CHECK_EQ(q.pop_next(200, false)->text, "Chair close ahead");
  CHECK_EQ(q.pop_next(200, false)->text, "Step left");
}

TEST_CASE("settings change the info interval at runtime") {
  SpeakQueue q;
  q.push(item(Priority::Info, "a", 0));
  CHECK(q.pop_next(0, false).has_value());
  SpeakQueueConfig cfg;
  cfg.info_min_interval_ms = 30000;  // wearer asked for a quieter hat
  q.set_config(cfg);
  q.push(item(Priority::Info, "b", 20000));
  CHECK(!q.pop_next(20000, false));
  q.push(item(Priority::Info, "c", 30000));
  CHECK_EQ(q.pop_next(30000, false)->text, "c");
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
  CHECK_EQ(q.pop_next(0, false)->text, "s2");

  SpeakQueue q2(cfg);
  q2.push(item(Priority::Info, "i1", 0));
  q2.push(item(Priority::Nav, "n1", 0));
  CHECK(q2.push(item(Priority::Safety, "s", 0)));    // evicts the info item
  CHECK_EQ(q2.evicted(), 1u);
  CHECK_EQ(q2.pop_next(0, false)->text, "s");
  CHECK_EQ(q2.pop_next(0, false)->text, "n1");
}

MINI_TEST_MAIN()
