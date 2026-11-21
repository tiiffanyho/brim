// Detection geometry: which side something is on, how close it looks, and what we are allowed to
// call it. The camera is the only sensor, so "close" is always a judgement about box area.
#include "detect/detector.hpp"
#include "mini_test.hpp"

using namespace brim;
using brim::detect::Box;
using brim::detect::Thing;
namespace s = brim::schema;

TEST_CASE("direction: thirds of the frame, by box centre") {
  const double m = 0.33;
  CHECK(detect::direction_of(Box{0.00f, 0.1f, 0.20f, 0.5f}, m) == s::Direction::Left);
  CHECK(detect::direction_of(Box{0.40f, 0.1f, 0.20f, 0.5f}, m) == s::Direction::Ahead);
  CHECK(detect::direction_of(Box{0.80f, 0.1f, 0.15f, 0.5f}, m) == s::Direction::Right);
  // A wide box straddling the frame is "ahead", not a side.
  CHECK(detect::direction_of(Box{0.10f, 0.1f, 0.80f, 0.5f}, m) == s::Direction::Ahead);
  // A nonsense margin falls back to thirds instead of misreporting a side.
  CHECK(detect::direction_of(Box{0.02f, 0.1f, 0.05f, 0.5f}, 0.0) == s::Direction::Left);
}

TEST_CASE("proximity is an estimate from box area, never a distance") {
  CHECK(detect::proximity_of(0.30, 0.18, 0.06) == s::Proximity::Close);
  CHECK(detect::proximity_of(0.18, 0.18, 0.06) == s::Proximity::Close);  // at the threshold
  CHECK(detect::proximity_of(0.10, 0.18, 0.06) == s::Proximity::Near);
  CHECK(detect::proximity_of(0.01, 0.18, 0.06) == s::Proximity::Far);
  CHECK(detect::proximity_of(0.0, 0.18, 0.06) == s::Proximity::Unknown);  // no box: say nothing
}

TEST_CASE("iou: overlap drives 'same thing as last frame'") {
  CHECK(detect::iou(Box{0, 0, 0.2f, 0.2f}, Box{0, 0, 0.2f, 0.2f}) > 0.99f);
  CHECK(detect::iou(Box{0, 0, 0.2f, 0.2f}, Box{0.5f, 0.5f, 0.2f, 0.2f}) == 0.0f);
  const float partial = detect::iou(Box{0, 0, 0.2f, 0.2f}, Box{0.1f, 0, 0.2f, 0.2f});
  CHECK(partial > 0.3f);
  CHECK(partial < 0.4f);
}

TEST_CASE("a phone is watched for but never called an obstacle") {
  CHECK(!detect::obstacle_kind_of(Thing::Phone).has_value());
  REQUIRE(detect::obstacle_kind_of(Thing::Chair).has_value());
  CHECK(*detect::obstacle_kind_of(Thing::Chair) == s::ObstacleKind::Chair);
  // Something is there but we are not sure what: "obstacle", never a guessed name.
  REQUIRE(detect::obstacle_kind_of(Thing::Unknown).has_value());
  CHECK(*detect::obstacle_kind_of(Thing::Unknown) == s::ObstacleKind::Obstacle);
  CHECK_EQ(std::string(detect::to_string(Thing::Unknown)), "obstacle");
}

TEST_CASE("the stub detector sees nothing and says it is healthy") {
  auto d = detect::make_stub_detector();
  REQUIRE(d != nullptr);
  CHECK(d->load());
  CHECK(d->healthy());
  capture::Frame f;
  CHECK_EQ(d->detect(f).size(), 0u);
}

MINI_TEST_MAIN()
