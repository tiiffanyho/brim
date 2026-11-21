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
// On the Pi 5 QNX image this reads /dev/thermal (text, degrees C); nullopt when unavailable.
std::optional<double> soc_temp_c();

}  // namespace brim::sysinfo
