#include "faces/face_detector.hpp"

#include "common/log.hpp"

namespace brim::faces {

namespace {
class StubFaceDetector final : public FaceDetector {
 public:
  bool load() override { return true; }
  std::vector<FaceBox> detect(const capture::Frame&) override { return {}; }
  const char* name() const override { return "stub"; }
};
}  // namespace

std::unique_ptr<FaceDetector> make_stub_face_detector() { return std::make_unique<StubFaceDetector>(); }

std::unique_ptr<FaceDetector> make_mediapipe_face_detector(const std::string& models_dir) {
  BRIM_LOGW("faces", "MediaPipe detector not implemented yet (models_dir=%s); using stub", models_dir.c_str());  // TODO(ai)
  return make_stub_face_detector();
}

}  // namespace brim::faces
