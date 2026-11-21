#include "common/sysinfo.hpp"

#include <sys/resource.h>

#include <cstdio>
#include <cstdlib>

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

// On the Pi 5 QNX image, `cat /dev/thermal` prints the SoC temperature in degrees C as text,
// e.g. "46.62" (verified on the Pi, Nov 21). Read it the same way. Anywhere else (the laptop) the
// file doesn't exist, so we return "unknown" instead of guessing.
std::optional<double> soc_temp_c() {
  std::FILE* f = std::fopen("/dev/thermal", "r");
  if (f == nullptr) return std::nullopt;
  char buf[32] = {0};
  const std::size_t n = std::fread(buf, 1, sizeof(buf) - 1, f);
  std::fclose(f);
  if (n == 0) return std::nullopt;
  char* end = nullptr;
  const double c = std::strtod(buf, &end);
  if (end == buf || c < -40.0 || c > 150.0) return std::nullopt;  // not a plausible chip temperature
  return c;
}

}  // namespace brim::sysinfo
