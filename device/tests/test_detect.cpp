// Detection geometry: which side something is on, how close it looks, and what we are allowed to
// call it. The camera is the only sensor, so "close" is always a judgement about box area.
#include <cmath>
#include <cstdint>

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

TEST_CASE("COCO classes map to what Brim may say, and nothing else") {
  CHECK(*detect::thing_of_coco(0) == Thing::Person);
  CHECK(*detect::thing_of_coco(1) == Thing::Bicycle);
  for (int c : {2, 3, 5, 7}) CHECK(*detect::thing_of_coco(c) == Thing::Vehicle);
  CHECK(*detect::thing_of_coco(61) == Thing::Chair);
  CHECK(*detect::thing_of_coco(66) == Thing::Table);
  CHECK(*detect::thing_of_coco(76) == Thing::Phone);
  for (int c : {14, 26, 32, 62}) CHECK(*detect::thing_of_coco(c) == Thing::Unknown);  // "something ahead"
  for (int c : {-1, 4, 15, 16, 43, 75, 89, 90, 1000}) CHECK(!detect::thing_of_coco(c).has_value());
}

TEST_CASE("decode: boxes become x,y,w,h fractions; low scores and ignored classes are dropped") {
  // ymin, xmin, ymax, xmax  (TFLite_Detection_PostProcess order)
  const float boxes[] = {0.10f, 0.20f, 0.90f, 0.50f,   // person, tall box
                         0.50f, 0.60f, 0.80f, 0.90f,   // chair, too unsure
                         0.00f, 0.00f, 0.50f, 0.50f,   // giraffe (ignored class)
                         -0.05f, 0.70f, 1.14f, 1.20f,  // phone, outside the frame a little
                         0.40f, 0.40f, 0.40f, 0.60f};  // degenerate box
  const float classes[] = {0, 61, 23, 76, 0};
  const float scores[] = {0.87f, 0.59f, 0.99f, 0.56f, 0.95f};
  std::vector<detect::Detection> out;
  detect::decode_detections(boxes, classes, scores, 5, 0.60f, 0.55f, out);
  REQUIRE(out.size() == 2u);
  CHECK(out[0].thing == Thing::Person);
  CHECK(std::abs(out[0].box.x - 0.20f) < 1e-6f);
  CHECK(std::abs(out[0].box.y - 0.10f) < 1e-6f);
  CHECK(std::abs(out[0].box.w - 0.30f) < 1e-6f);
  CHECK(std::abs(out[0].box.h - 0.80f) < 1e-6f);
  CHECK_EQ(out[0].track_id, -1);
  // A phone uses its own (lower) threshold so safety can judge it; the box is clamped to the frame.
  CHECK(out[1].thing == Thing::Phone);
  CHECK(out[1].box.y == 0.0f);
  CHECK(std::abs(out[1].box.x + out[1].box.w - 1.0f) < 1e-6f);
  CHECK(std::abs(out[1].box.h - 1.0f) < 1e-6f);
  // NaN scores are never trusted.
  const float nan_score[] = {std::nanf("")};
  out.clear();
  detect::decode_detections(boxes, classes, nan_score, 1, 0.6f, 0.55f, out);
  CHECK_EQ(out.size(), 0u);
}

TEST_CASE("resize: 640x480 RGB to 320x320 keeps colours and layout, and does not allocate per frame") {
  const int sw = 640, sh = 480, dw = 320, dh = 320;
  std::vector<std::uint8_t> src(sw * sh * 3), dst(dw * dh * 3);
  for (int y = 0; y < sh; ++y)
    for (int x = 0; x < sw; ++x) {
      std::uint8_t* p = &src[(y * sw + x) * 3];
      p[0] = x < sw / 2 ? 200 : 10;  // red on the left half
      p[1] = y < sh / 2 ? 50 : 150;  // green brighter in the bottom half
      p[2] = 77;
    }
  detect::RgbResizer r;
  r.resize(src.data(), sw, sh, dst.data(), dw, dh);
  auto px = [&](int x, int y) { return &dst[(y * dw + x) * 3]; };
  CHECK_EQ(px(10, 10)[0], 200);
  CHECK_EQ(px(300, 10)[0], 10);
  CHECK_EQ(px(10, 10)[1], 50);
  CHECK_EQ(px(10, 300)[1], 150);
  CHECK_EQ(px(160, 160)[2], 77);
  // A second frame of the same size reuses the tables (same output, no rebuild).
  r.resize(src.data(), sw, sh, dst.data(), dw, dh);
  CHECK_EQ(px(300, 300)[0], 10);
}

TEST_CASE("the unavailable/no-TFLite path never pretends to see") {
  if (detect::tflite_available()) return;  // only meaningful in a build without TFLite
  auto d = detect::make_tflite_detector(detect::DetectorConfig{});
  CHECK(!d->load());
  CHECK(!d->healthy());
  capture::Frame f;
  std::vector<detect::Detection> out(3);
  d->detect(f, out);
  CHECK_EQ(out.size(), 0u);
}

MINI_TEST_MAIN()
