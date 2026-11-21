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
#include "nav/nav.hpp"

namespace brim::safety {

class SafetyMonitor {
 public:
  virtual ~SafetyMonitor() = default;
  virtual bool healthy() const = 0;
  // Returns alerts for this frame (the caller stamps seq/source/t_mono_ms). `nav_now` is the
  // navigation call that currently stands (nav::Navigator::current(), nullptr while unsure): a
  // close warning says that call's clear side, or stop when there is none.
  // `subjects`, if given, gets one entry per alert: the track id of the box it is about (-1 = none),
  // so an enrolled person can be named.
  virtual std::vector<schema::SafetyAlertEvent> check(const capture::Frame& frame,
                                                      const std::vector<detect::Detection>& dets,
                                                      const nav::FrameDecision* nav_now,
                                                      std::vector<int>* subjects = nullptr) = 0;
};

std::unique_ptr<SafetyMonitor> make_stub_safety_monitor();

// person_close / object_close from the boxes (no distance sensor): a confident box
// (min_confidence) that is an obstacle (never a phone, as in src/nav), overlaps the corridor
// (the middle band of nav.json side_margin_frac, lower half, the same corridor as src/nav) and fills
// at least person_close_area_frac / object_close_area_frac of the frame. It must hold for
// confirm_frames frames in a row, and each kind is repeated at most every repeat_suppress_s.
// The words are one phrase with what to do (nav::warning_text): "Person ahead, move left",
// "Chair ahead, move right", "Person ahead, stop", "Path unclear, stop".
// TODO(ai): phone_raised from a held-phone classifier on the "cell phone" class plus hand
//   landmarks (mediapipe 0.10.26, tflite-runtime 2.21.0 from oss.qnx.com).
std::unique_ptr<SafetyMonitor> make_vision_safety_monitor(const config::SafetyThresholds& safety,
                                                          const config::NavThresholds& nav);

}  // namespace brim::safety
