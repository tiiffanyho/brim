#include "common/timing.hpp"

#include <cerrno>
#include <ctime>

#include "common/log.hpp"

namespace brim::timing {

void sleep_until_ns(std::int64_t deadline_ns) {
#if defined(__APPLE__)
  for (;;) {
    std::int64_t left = deadline_ns - mono_ns();
    if (left <= 0) return;
    timespec ts{static_cast<time_t>(left / 1000000000LL), static_cast<long>(left % 1000000000LL)};
    if (nanosleep(&ts, nullptr) == 0) return;
    if (errno != EINTR) return;
  }
#else
  timespec ts{static_cast<time_t>(deadline_ns / 1000000000LL), static_cast<long>(deadline_ns % 1000000000LL)};
  while (clock_nanosleep(CLOCK_MONOTONIC, TIMER_ABSTIME, &ts, nullptr) == EINTR) {
  }
#endif
}

StageTimer::~StageTimer() {
  auto ms = d_.elapsed_ms();
  if (ms > budget_ms_) BRIM_LOGW("timing", "stage %s overran: %lld ms > %lld ms budget", stage_, (long long)ms, (long long)budget_ms_);
}

}  // namespace brim::timing
