// Safety: the three things that interrupt everything else, all judged from the camera image —
//   person_close   a person is close in front of the wearer
//   object_close   something else is close in front of the wearer
//   phone_raised   a phone is held up pointing at the wearer
// Highest speech priority; navigation comes after (see src/nav), never before.
//
// There is no distance sensor on the hat: the camera is the only way to see an obstacle, so every
// call here is an estimate from how much of the frame a box fills (detect::proximity_of).
//
// Design rule (cannot-fail): if detection is down, report a `fault` (degraded) and keep the rest of
// the pipeline running — never silently pretend the path is clear.
#pragma once

#include <memory>
#include <vector>

#include "capture/camera.hpp"
#include "common/config.hpp"
#include "detect/detector.hpp"
#include "events.gen.hpp"

namespace brim::safety {

class SafetyMonitor {
 public:
  virtual ~SafetyMonitor() = default;
  virtual bool healthy() const = 0;
  // Returns alerts for this frame (the caller stamps seq/source/t_mono_ms).
  virtual std::vector<schema::SafetyAlertEvent> check(const capture::Frame& frame,
                                                      const std::vector<detect::Detection>& dets) = 0;
};

std::unique_ptr<SafetyMonitor> make_stub_safety_monitor();

// TODO(ai): decide person_close / object_close from the detection boxes (area fraction vs
//   SafetyThresholds, direction from NavThresholds) and phone_raised from a held-phone classifier
//   on the MediaPipe object detector's "cell phone" class plus hand landmarks (mediapipe 0.10.26,
//   tflite-runtime 2.21.0 from oss.qnx.com). Hold an alert for repeat_suppress_s so one obstacle is
//   not announced every frame. object_close must use detect::is_obstacle(): a traffic light is
//   never an obstacle and never raises a safety alert.
std::unique_ptr<SafetyMonitor> make_vision_safety_monitor(const config::SafetyThresholds& safety,
                                                          const config::NavThresholds& nav);

}  // namespace brim::safety
