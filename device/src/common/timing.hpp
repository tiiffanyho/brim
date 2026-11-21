// Monotonic time + deadline helpers. Everything real-time uses CLOCK_MONOTONIC (POSIX; present
// on QNX 8.0 and macOS/Linux), never wall-clock time.
#pragma once

#include <cstdint>
#include <ctime>

namespace brim::timing {

inline std::int64_t mono_ns() {
  timespec ts{};
  clock_gettime(CLOCK_MONOTONIC, &ts);
  return static_cast<std::int64_t>(ts.tv_sec) * 1000000000LL + ts.tv_nsec;
}
inline std::int64_t mono_ms() { return mono_ns() / 1000000LL; }

// Sleep until an absolute monotonic time (ns). Retries on EINTR.
// clock_nanosleep(TIMER_ABSTIME) is POSIX and exists on QNX; macOS lacks it, so fall back.
void sleep_until_ns(std::int64_t deadline_ns);
inline void sleep_ms(std::int64_t ms) { sleep_until_ns(mono_ns() + ms * 1000000LL); }

// Fixed-rate loop helper: wait() sleeps until the next period boundary and reports overruns,
// so a 15 fps capture loop does not drift.
class PeriodicTimer {
 public:
  explicit PeriodicTimer(std::int64_t period_ms) : period_ns_(period_ms * 1000000LL), next_ns_(mono_ns()) {}
  // Returns the number of periods that were missed (0 = on time).
  int wait() {
    next_ns_ += period_ns_;
    std::int64_t now = mono_ns();
    int missed = 0;
    while (next_ns_ < now) {  // overran: skip ahead instead of bursting
      next_ns_ += period_ns_;
      ++missed;
    }
    sleep_until_ns(next_ns_);
    return missed;
  }

 private:
  std::int64_t period_ns_;
  std::int64_t next_ns_;
};

// A budget for one pipeline stage. Check expired()/remaining_ms() between steps.
class Deadline {
 public:
  explicit Deadline(std::int64_t budget_ms) : start_ns_(mono_ns()), budget_ns_(budget_ms * 1000000LL) {}
  bool expired() const { return mono_ns() - start_ns_ > budget_ns_; }
  std::int64_t elapsed_ms() const { return (mono_ns() - start_ns_) / 1000000LL; }
  std::int64_t remaining_ms() const { return (budget_ns_ - (mono_ns() - start_ns_)) / 1000000LL; }

 private:
  std::int64_t start_ns_;
  std::int64_t budget_ns_;
};

// Logs a warning if the enclosing scope takes longer than budget_ms.
class StageTimer {
 public:
  StageTimer(const char* stage, std::int64_t budget_ms) : stage_(stage), d_(budget_ms), budget_ms_(budget_ms) {}
  ~StageTimer();
  std::int64_t elapsed_ms() const { return d_.elapsed_ms(); }

 private:
  const char* stage_;
  Deadline d_;
  std::int64_t budget_ms_;
};

}  // namespace brim::timing
