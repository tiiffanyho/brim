#include "audio/speak_queue.hpp"
#include "mini_test.hpp"

using namespace brim::audio;

static SpeechItem item(Priority p, const char* text, std::int64_t t) { return SpeechItem{p, text, "", t}; }

TEST_CASE("priority order: safety > name > cue") {
  SpeakQueue q;
  q.push(item(Priority::Cue, "cue", 0));
  q.push(item(Priority::Name, "name", 0));
  q.push(item(Priority::Safety, "safety", 0));
  CHECK_EQ(q.pop_next(100, false)->text, "safety");
  CHECK_EQ(q.pop_next(100, false)->text, "name");
  CHECK_EQ(q.pop_next(100, false)->text, "cue");
  CHECK(!q.pop_next(100, false));
}

TEST_CASE("FIFO within a priority") {
  SpeakQueue q;
  q.push(item(Priority::Name, "Sam", 0));
  q.push(item(Priority::Name, "Priya", 1));
  CHECK_EQ(q.pop_next(10, false)->text, "Sam");
  CHECK_EQ(q.pop_next(10, false)->text, "Priya");
}

TEST_CASE("cues wait for no-one-talking") {
  SpeakQueue q;
  q.push(item(Priority::Cue, "waving", 1000));
  CHECK(!q.pop_next(1500, /*someone_talking=*/true));
  auto it = q.pop_next(1600, false);
  REQUIRE(it.has_value());
  CHECK_EQ(it->text, "waving");
}

TEST_CASE("safety and names do not wait for silence") {
  SpeakQueue q;
  q.push(item(Priority::Safety, "step", 0));
  q.push(item(Priority::Name, "Sam", 0));
  CHECK_EQ(q.pop_next(10, true)->text, "step");
  CHECK_EQ(q.pop_next(10, true)->text, "Sam");
}

TEST_CASE("cue older than 3 s is dropped") {
  SpeakQueue q;
  q.push(item(Priority::Cue, "smiling", 1000));
  CHECK(!q.pop_next(4001, false));  // 3001 ms old
  CHECK_EQ(q.size(), 0u);
  CHECK_EQ(q.dropped_stale_cues(), 1u);
}

TEST_CASE("cue exactly 3 s old is still spoken") {
  SpeakQueue q;
  q.push(item(Priority::Cue, "smiling", 1000));
  CHECK(q.pop_next(4000, false).has_value());
}

TEST_CASE("stale cue dropped while waiting for silence") {
  SpeakQueue q;
  q.push(item(Priority::Cue, "waving", 0));
  CHECK(!q.pop_next(1000, true));
  CHECK(!q.pop_next(2000, true));
  CHECK(!q.pop_next(3500, false));  // silence came too late
  CHECK_EQ(q.dropped_stale_cues(), 1u);
}

TEST_CASE("at most one cue per 10 s") {
  SpeakQueue q;
  q.push(item(Priority::Cue, "a", 0));
  CHECK(q.pop_next(0, false).has_value());
  q.push(item(Priority::Cue, "b", 5000));
  CHECK(!q.pop_next(5000, false));   // only 5 s since last cue
  CHECK(!q.pop_next(9999, false));   // still rate-limited (and b now 4999 ms old -> dropped)
  q.push(item(Priority::Cue, "c", 9500));
  auto it = q.pop_next(10000, false);
  REQUIRE(it.has_value());
  CHECK_EQ(it->text, "c");
}

TEST_CASE("rate limit does not block names or safety") {
  SpeakQueue q;
  q.push(item(Priority::Cue, "a", 0));
  CHECK(q.pop_next(0, false).has_value());
  q.push(item(Priority::Name, "Sam", 100));
  q.push(item(Priority::Safety, "curb", 100));
  CHECK_EQ(q.pop_next(200, false)->text, "curb");
  CHECK_EQ(q.pop_next(200, false)->text, "Sam");
}

TEST_CASE("capacity: evicts lowest priority, never safety for a cue") {
  SpeakQueueConfig cfg;
  cfg.capacity = 2;
  SpeakQueue q(cfg);
  CHECK(q.push(item(Priority::Safety, "s1", 0)));
  CHECK(q.push(item(Priority::Safety, "s2", 0)));
  CHECK(!q.push(item(Priority::Cue, "c", 0)));       // rejected: would need to evict safety
  CHECK(q.push(item(Priority::Safety, "s3", 0)));     // evicts oldest safety
  CHECK_EQ(q.size(), 2u);
  CHECK_EQ(q.pop_next(0, false)->text, "s2");

  SpeakQueue q2(cfg);
  q2.push(item(Priority::Cue, "c1", 0));
  q2.push(item(Priority::Name, "n1", 0));
  CHECK(q2.push(item(Priority::Safety, "s", 0)));    // evicts the cue
  CHECK_EQ(q2.evicted(), 1u);
  CHECK_EQ(q2.pop_next(0, false)->text, "s");
  CHECK_EQ(q2.pop_next(0, false)->text, "n1");
}

MINI_TEST_MAIN()
