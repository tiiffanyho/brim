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

// Real camera on the Pi 5 under QNX 8.0 (no V4L2 on QNX): the QNX Camera Framework (libcamapi)
// reads the C270 through the `sensor` service, camera unit 5 by default (SENSOR_UNIT_5 in
// /etc/config/sensor/sensor_rpi5.conf). Override with BRIM_CAMERA_UNIT=<n>. See camera_qnx.cpp.
// grab() returns false after 1 s without a frame and reopens the camera every 2 s until it is back.
std::unique_ptr<Camera> make_qnx_camera(int fps);

}  // namespace brim::capture
