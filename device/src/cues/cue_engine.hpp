// Social cues (waving, smiling, approaching, ...) and name-announcement de-duplication.
#pragma once

#include <cstdint>
#include <map>
#include <memory>
#include <optional>
#include <string>
#include <vector>

#include "capture/camera.hpp"
#include "common/config.hpp"
#include "events.gen.hpp"
#include "faces/face_detector.hpp"

namespace brim::cues {

struct TrackedPerson {
  int track_id = -1;
  std::optional<std::int64_t> person_id;  // set if recognized
  std::optional<std::string> anon_id;     // set if stranger
  std::string display_name;               // "" for strangers
  faces::FaceBox box;
};

class CueEngine {
 public:
  virtual ~CueEngine() = default;
  // Returns cue events (without seq/source/t_mono_ms; the caller stamps them).
  virtual std::vector<schema::CueEvent> update(const capture::Frame& frame, const std::vector<TrackedPerson>& people) = 0;
};

std::unique_ptr<CueEngine> make_stub_cue_engine();
// TODO(ai): MediaPipe hand/pose landmarks (mediapipe 0.10.26) -> waving; face blendshapes ->
//   smiling; box-size trend over ~1 s -> approaching/leaving; head-pose yaw -> looking_at_you.
//   Thresholds come from config/cues.json (common/config.hpp CueThresholds).
std::unique_ptr<CueEngine> make_mediapipe_cue_engine(const config::CueThresholds& t);

// "Sam is here" should be said once when Sam arrives, not every frame.
class NameAnnouncer {
 public:
  explicit NameAnnouncer(std::int64_t repeat_after_ms) : repeat_after_ms_(repeat_after_ms) {}
  bool should_announce(std::int64_t person_id, std::int64_t now_ms);
  void forget(std::int64_t person_id) { last_.erase(person_id); }

 private:
  std::int64_t repeat_after_ms_;
  std::map<std::int64_t, std::int64_t> last_;
};

}  // namespace brim::cues
