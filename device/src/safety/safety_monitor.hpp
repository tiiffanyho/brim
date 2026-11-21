// Safety: head-height obstacles, steps/curbs, fast-approaching objects. Highest speech priority.
// Design rule (cannot-fail): if the safety sensor/model is down, report a `fault` (degraded) and
// keep the rest of the pipeline running — never silently pretend the path is clear.
#pragma once

#include <memory>
#include <vector>

#include "capture/camera.hpp"
#include "common/config.hpp"
#include "events.gen.hpp"

namespace brim::safety {

class SafetyMonitor {
 public:
  virtual ~SafetyMonitor() = default;
  virtual bool healthy() const = 0;
  // Returns alerts (caller stamps seq/source/t_mono_ms).
  virtual std::vector<schema::SafetyAlertEvent> check(const capture::Frame& frame) = 0;
};

std::unique_ptr<SafetyMonitor> make_stub_safety_monitor();
// TODO(hw): distance sensor at head height (e.g. VL53L1X ToF over I2C, or ultrasonic).
//   TODO(qnx-check): I2C on QNX is a resource manager (/dev/i2c*, devctl DCMD_I2C_SEND/RECV via
//   <hw/i2c.h>), not Linux i2c-dev. Confirm the Pi 5 BSP starts i2c-bcm2711/rpi driver.
// TODO(ai): monocular depth / object detection model (.tflite) for curbs and vehicles.
std::unique_ptr<SafetyMonitor> make_sensor_safety_monitor(const config::CueThresholds& t);

}  // namespace brim::safety
