// Detection: a camera frame -> boxes for people and objects, plus anonymous tracks.
//
// Privacy rules enforced here, and they are the whole point of this module:
//   * no face recognition, no face embeddings, no identity lookup of any kind;
//   * a person is a box with a track_id, never a name — the track_id says "the same person as a
//     moment ago", nothing more, and it is meaningless once the track is dropped;
//   * boxes are kept only as long as a track is alive (seconds), and TrackStore::expire() clears
//     every track within track_forget_s (30 minutes, capped in common/config.cpp);
//   * nothing here is ever written to disk — frames and boxes live in memory only.
//
// There is no distance sensor on the hat, so "how close" is always estimated from how much of the
// frame a box fills (see proximity_of).
#pragma once

#include <cstdint>
#include <map>
#include <memory>
#include <optional>
#include <string>
#include <vector>

#include "capture/camera.hpp"
#include "events.gen.hpp"

namespace brim::detect {

// Normalized [0,1] image coordinates: x,y is the top-left corner.
struct Box {
  float x = 0, y = 0, w = 0, h = 0;
  float cx() const { return x + w / 2; }
  float cy() const { return y + h / 2; }
  float area() const { return w * h; }
};

// Intersection over union, used to decide "same thing as last frame".
float iou(const Box& a, const Box& b);

// What the detector found. Kept separate from schema::ObstacleKind because a phone is something we
// watch for (a privacy alert) but never something to walk around, and a traffic light (feasibility
// test, see detect/traffic_light.hpp) is something we report but never something in the way.
enum class Thing { Person, Phone, Chair, Table, Door, Pole, Stairs, Vehicle, Bicycle, TrafficLight, Unknown };
const char* to_string(Thing t);

// What we are allowed to call this out loud. Phone and TrafficLight -> nullopt (never announced as
// an obstacle); Unknown -> ObstacleKind::Obstacle, i.e. "something is there" without naming it.
std::optional<schema::ObstacleKind> obstacle_kind_of(Thing t);

// Does this thing count for the clear-path and safety corridor logic? False for a phone (watched
// by safety as a privacy alert) and a traffic light (reported, never in the way).
bool is_obstacle(Thing t);

struct Detection {
  Thing thing = Thing::Unknown;
  Box box;
  float score = 0;
  int track_id = -1;  // filled in by TrackStore; -1 = not tracked yet
  bool is_person() const { return thing == Thing::Person; }
};

// How long each detection stage took on the last frame (ms). Logged against the 30 ms budget.
struct StageTimes {
  double resize_ms = 0;  // RGB888 frame -> model input tensor
  double invoke_ms = 0;  // TfLiteInterpreterInvoke
  double decode_ms = 0;  // output tensors -> Detection list
  double total() const { return resize_ms + invoke_ms + decode_ms; }
};

class Detector {
 public:
  virtual ~Detector() = default;
  virtual bool load() = 0;
  // Allocation-free form for the per-frame path: `out` is cleared and refilled, its capacity reused.
  virtual void detect(const capture::Frame& frame, std::vector<Detection>& out) { out = detect(frame); }
  // Convenience form (tests, tools). Allocates a new vector each call.
  virtual std::vector<Detection> detect(const capture::Frame& frame) = 0;
  virtual const char* name() const = 0;
  // Cannot-fail rule: a detector that is not healthy must say so, so brim can report a fault and
  // speak it, instead of silently pretending the path is clear.
  virtual bool healthy() const = 0;
  virtual StageTimes last_stage_times() const { return {}; }
};

// Returns no detections. The laptop's --fake mode injects events further down the pipeline instead.
std::unique_ptr<Detector> make_stub_detector();

// Person + object detector: EfficientDet-Lite0 (uint8 quantized, with the in-graph
// TFLite_Detection_PostProcess op) on tflite-runtime 2.21.0 from https://oss.qnx.com, via the
// TFLite C API. See models/README.md for the model's source, tensors and budget.
// No face model of any kind is loaded here.
struct DetectorConfig {
  std::string model_path = "models/efficientdet_lite0.tflite";
  int threads = 4;
  float min_confidence = 0.60f;        // safety.json min_confidence: below this a box is dropped
  float phone_min_confidence = 0.55f;  // safety.json phone_min_confidence: phones only, for the safety alert
  float traffic_min_confidence = 0.60f;  // traffic.json min_score: traffic lights only
};
// When brim is built without TFLite (BRIM_WITH_TFLITE=OFF) this returns a detector whose load()
// fails and which reports itself unhealthy, so brim speaks "cannot see" instead of guessing.
std::unique_ptr<Detector> make_tflite_detector(const DetectorConfig& cfg);
bool tflite_available();  // was this binary built with TFLite?

// --- pure helpers used by the TFLite detector (host-testable, no TFLite needed) ---

// COCO-90 label index (0-based, as the model's labelmap.txt) -> what Brim cares about.
// nullopt = a class we ignore entirely (never named, never an obstacle).
std::optional<Thing> thing_of_coco(int coco_index);

// Bilinear resize of a packed RGB888 image into another packed RGB888 buffer (stretch, no
// letterbox: boxes come back as fractions of the original frame unchanged). Lookup tables are
// rebuilt only when the sizes change, so steady-state frames do not allocate.
class RgbResizer {
 public:
  void resize(const std::uint8_t* src, int src_w, int src_h, std::uint8_t* dst, int dst_w, int dst_h);

 private:
  void build(int src_w, int src_h, int dst_w, int dst_h);
  int sw_ = 0, sh_ = 0, dw_ = 0, dh_ = 0;
  std::vector<int> x0_, x1_, y0_, y1_;        // source byte offset (x) / row (y)
  std::vector<std::uint16_t> fx_, fy_;        // 8-bit fixed-point weights of the second sample
};

// TFLite_Detection_PostProcess outputs -> detections. boxes are [ymin, xmin, ymax, xmax] in
// [0,1] (clamped here; the model can return slightly outside), classes are COCO-90 indices.
// Appends to `out` (does not clear). Ignored classes and low scores are dropped. Traffic lights use
// traffic_min_confidence; a negative value means "same as min_confidence".
void decode_detections(const float* boxes, const float* classes, const float* scores, int count,
                       float min_confidence, float phone_min_confidence, std::vector<Detection>& out,
                       float traffic_min_confidence = -1.0f);

// Runs the detector every frame, or every 2nd frame while it is over budget (with hysteresis so
// it does not flap). fixed_stride 1 or 2 forces that stride; 0 = automatic.
class DetectPacer {
 public:
  DetectPacer(double budget_ms, int fixed_stride) : budget_ms_(budget_ms), fixed_(fixed_stride) {
    if (fixed_ == 1 || fixed_ == 2) stride_ = fixed_;
  }
  // Call once per camera frame; true = run the detector on this one.
  bool tick() { return (counter_++ % static_cast<std::uint64_t>(stride_)) == 0; }
  // Feed the detect time of each run. Returns true when the stride just changed.
  bool record(double ms);
  int stride() const { return stride_; }
  double average_ms() const { return avg_ms_; }

 private:
  double budget_ms_;
  int fixed_;
  int stride_ = 1;
  std::uint64_t counter_ = 0;
  double avg_ms_ = -1;
};

// Average / max per stage over a window, for one log line every few seconds.
struct StageStats {
  int runs = 0;
  StageTimes sum, max;
  double track_sum = 0, track_max = 0;
  void add(const StageTimes& t, double track_ms);
  void reset() { *this = StageStats{}; }
};

// Which third of the frame something is in. side_margin_frac comes from config/nav.json.
schema::Direction direction_of(const Box& b, double side_margin_frac);

// Image-based closeness estimate. Everything smaller than near_frac is "far".
schema::Proximity proximity_of(double area_frac, double close_frac, double near_frac);

// Anonymous tracks. Matches this frame's boxes to recent tracks of the same Thing by overlap
// (IoU), or — for something moving fast or a frame the detector skipped — by a short centre jump
// between boxes of similar size. Best matches are taken first, each track and box used once.
// Hands out meaningless increasing ids and forgets everything it has not seen for forget_after_ms.
// It holds no appearance data at all — only the last box, so there is nothing to recognize anyone
// with, in memory or anywhere else.
class TrackStore {
 public:
  explicit TrackStore(std::int64_t forget_after_ms = 30 * 60 * 1000, std::int64_t repeat_suppress_ms = 10000,
                      float same_thing_iou = 0.3f, std::int64_t match_window_ms = 1500, float center_gate = 0.6f)
      : forget_after_ms_(forget_after_ms),
        repeat_suppress_ms_(repeat_suppress_ms),
        same_iou_(same_thing_iou),
        match_window_ms_(match_window_ms),
        center_gate_(center_gate) {
    pairs_.reserve(64);
    det_taken_.reserve(32);
  }

  // Assigns track ids to `dets` in place (and updates each track's last-seen time).
  void observe(std::vector<Detection>& dets, std::int64_t now_ms);
  // Drops tracks not seen for forget_after_ms. Call every frame; it is cheap.
  void expire(std::int64_t now_ms);
  // Rate limit for speech about one track: true at most once per repeat_suppress_ms.
  bool should_announce(int track_id, std::int64_t now_ms);
  // Forget everything right now (used when the pipeline restarts or the camera goes away).
  void clear();
  std::size_t size() const { return tracks_.size(); }
  // Tracks seen within the match window: things actually in view right now.
  std::size_t active(std::int64_t now_ms) const;
  // >1 = overlap match (1 + IoU), (0,1) = centre-jump match, 0 = not the same thing. Exposed for tests.
  float match_score(const Box& track_box, const Box& det_box) const;

 private:
  struct Track {
    Thing thing = Thing::Unknown;
    Box box;
    std::int64_t last_seen_ms = 0;
    std::int64_t last_announced_ms = -1;
    std::uint64_t matched_in_ = 0;  // observe() call that last claimed this track
  };
  struct Pair {
    float score;
    int det;
    int id;
  };
  std::int64_t forget_after_ms_;
  std::int64_t repeat_suppress_ms_;
  float same_iou_;
  std::int64_t match_window_ms_;
  float center_gate_;
  std::map<int, Track> tracks_;
  int next_id_ = 1;
  std::uint64_t observe_count_ = 0;
  std::vector<Pair> pairs_;       // reused every frame
  std::vector<char> det_taken_;   // reused every frame
};

}  // namespace brim::detect
