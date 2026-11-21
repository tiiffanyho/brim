// Face detection: frame -> face boxes (+ landmarks for alignment and cues).
#pragma once

#include <array>
#include <memory>
#include <string>
#include <vector>

#include "capture/camera.hpp"

namespace brim::faces {

struct FaceBox {
  float x = 0, y = 0, w = 0, h = 0;  // normalized [0,1] image coordinates
  float score = 0;
  std::array<float, 12> landmarks{};  // 6 keypoints (eyes, nose, mouth, ears) x,y normalized
  int track_id = -1;                  // stable across frames (simple IoU tracker), -1 = untracked
};

class FaceDetector {
 public:
  virtual ~FaceDetector() = default;
  virtual bool load() = 0;
  virtual std::vector<FaceBox> detect(const capture::Frame& frame) = 0;
  virtual const char* name() const = 0;
};

// Returns no faces. Fake mode injects events further down the pipeline instead.
std::unique_ptr<FaceDetector> make_stub_face_detector();

// TODO(ai): MediaPipe BlazeFace (short-range) via mediapipe 0.10.26 / tflite-runtime 2.21.0 from
//   oss.qnx.com. Model: models/blaze_face_short_range.tflite (see models/README.md).
//   Budget: <= 15 ms per 640x480 frame on the Pi 5 CPU (4 threads). Add an IoU tracker so
//   recognition runs once per track, not every frame.
std::unique_ptr<FaceDetector> make_mediapipe_face_detector(const std::string& models_dir);

}  // namespace brim::faces
