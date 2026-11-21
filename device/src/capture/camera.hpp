// Camera capture. Frames live in memory only — Brim never records or stores video.
#pragma once

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace brim::capture {

struct Frame {
  int width = 0;
  int height = 0;
  std::vector<std::uint8_t> rgb;     // packed RGB888, width*height*3 (reused buffer, no per-frame alloc)
  std::int64_t t_capture_mono_ms = 0;  // CLOCK_MONOTONIC when the sensor delivered the frame
  std::uint64_t index = 0;
};

class Camera {
 public:
  virtual ~Camera() = default;
  virtual bool open() = 0;
  // Blocks until the next frame (paced at the configured fps). false = camera failure.
  virtual bool grab(Frame& out) = 0;
  virtual const char* name() const = 0;
};

// Synthetic grey frames at `fps`, used on the laptop and in fake mode.
std::unique_ptr<Camera> make_fake_camera(int fps, int width = 640, int height = 480);

// TODO(hw): real camera on the Pi 5 under QNX 8.0. No V4L2 on QNX!
//   Options to evaluate (TODO(qnx-check) on the Pi):
//   1. QNX Camera Framework / Sensor Framework (libcamapi: camera_open(), camera_start_viewfinder()
//      or camera_register_resource + buffer callbacks) — check `ls /dev/camera*`, and whether the
//      Pi 5 BSP ships a sensor config (sensor.conf) for the CSI camera module.
//   2. USB UVC webcam via the QNX USB camera driver, if present in the BSP (`usb -v`).
//   Convert to RGB with OpenCV 4.12 (oss.qnx.com) cv::cvtColor if the sensor gives YUV/NV12.
std::unique_ptr<Camera> make_qnx_camera(int fps);

}  // namespace brim::capture
