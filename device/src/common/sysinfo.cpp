#include "common/sysinfo.hpp"

#include <sys/resource.h>

#include "common/timing.hpp"

namespace brim::sysinfo {

double CpuMeter::sample() {
  rusage ru{};
  getrusage(RUSAGE_SELF, &ru);
  double cpu = ru.ru_utime.tv_sec + ru.ru_utime.tv_usec / 1e6 + ru.ru_stime.tv_sec + ru.ru_stime.tv_usec / 1e6;
  double wall = timing::mono_ns() / 1e9;
  double pct = 0;
  if (last_cpu_s_ >= 0 && wall > last_wall_s_) pct = 100.0 * (cpu - last_cpu_s_) / (wall - last_wall_s_);
  last_cpu_s_ = cpu;
  last_wall_s_ = wall;
  return pct;
}

std::optional<double> soc_temp_c() { return std::nullopt; }  // TODO(hw)

}  // namespace brim::sysinfo
