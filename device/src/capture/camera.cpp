#include "capture/camera.hpp"

#include "common/log.hpp"
#include "common/timing.hpp"

namespace brim::capture {

namespace {

class FakeCamera final : public Camera {
 public:
  FakeCamera(int fps, int w, int h) : timer_(1000 / (fps > 0 ? fps : 15)), w_(w), h_(h) {}
  bool open() override { return true; }
  bool grab(Frame& out) override {
    timer_.wait();
    out.width = w_;
    out.height = h_;
    out.rgb.resize(static_cast<std::size_t>(w_) * h_ * 3, 128);  // grey; resize is a no-op after frame 1
    out.t_capture_mono_ms = timing::mono_ms();
    out.index = ++index_;
    return true;
  }
  const char* name() const override { return "fake"; }

 private:
  timing::PeriodicTimer timer_;
  int w_, h_;
  std::uint64_t index_ = 0;
};

}  // namespace

std::unique_ptr<Camera> make_fake_camera(int fps, int width, int height) {
  return std::make_unique<FakeCamera>(fps, width, height);
}

#if !BRIM_WITH_CAMAPI
// Built without libcamapi (laptop, or a QNX image without the camera headers): fall back to fake.
std::unique_ptr<Camera> make_qnx_camera(int fps) {
  BRIM_LOGW("capture", "built without libcamapi; using fake camera");
  return make_fake_camera(fps);
}
#endif  // the real one is in camera_qnx.cpp

}  // namespace brim::capture
