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
// watch for (a privacy alert) but never something to walk around.
enum class Thing { Person, Phone, Chair, Table, Door, Pole, Stairs, Vehicle, Bicycle, Unknown };
const char* to_string(Thing t);

// What we are allowed to call this out loud. Phone -> nullopt (never announced as an obstacle);
// Unknown -> ObstacleKind::Obstacle, i.e. "something is there" without naming it.
std::optional<schema::ObstacleKind> obstacle_kind_of(Thing t);

struct Detection {
  Thing thing = Thing::Unknown;
  Box box;
  float score = 0;
  int track_id = -1;  // filled in by TrackStore; -1 = not tracked yet
  bool is_person() const { return thing == Thing::Person; }
};

class Detector {
 public:
  virtual ~Detector() = default;
  virtual bool load() = 0;
  virtual std::vector<Detection> detect(const capture::Frame& frame) = 0;
  virtual const char* name() const = 0;
  // Cannot-fail rule: a detector that is not healthy must say so, so brim can report a fault and
  // speak it, instead of silently pretending the path is clear.
  virtual bool healthy() const = 0;
};

// Returns no detections. The laptop's --fake mode injects events further down the pipeline instead.
std::unique_ptr<Detector> make_stub_detector();

// TODO(ai): person + object detection with the open-source modules from https://oss.qnx.com:
//   MediaPipe 0.10.26 object detector (EfficientDet-Lite0, int8) on tflite-runtime 2.21.0, frames
//   converted with OpenCV 4.12. Model: models/efficientdet_lite0.tflite (see models/README.md).
//   Budget: <= 30 ms per 640x480 frame on the Pi 5 CPU (4 threads), <= 66 ms for the whole frame.
//   Keep the per-frame path allocation-free: reuse the input tensor and the detection vector.
std::unique_ptr<Detector> make_mediapipe_detector(const std::string& models_dir);

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
