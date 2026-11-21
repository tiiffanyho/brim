// Process/system stats for health events. POSIX only.
#pragma once

#include <optional>

namespace brim::sysinfo {

// CPU% of THIS process since the previous call (getrusage; POSIX, available on QNX).
class CpuMeter {
 public:
  double sample();

 private:
  double last_cpu_s_ = -1;
  double last_wall_s_ = 0;
};

// SoC temperature in °C if known.
// TODO(hw)/TODO(qnx-check): no /sys/class/thermal on QNX. Find how the Pi 5 BSP exposes the
//   BCM2712 thermal sensor (a devctl on a /dev/ node, or the mailbox property interface).
std::optional<double> soc_temp_c();

}  // namespace brim::sysinfo
