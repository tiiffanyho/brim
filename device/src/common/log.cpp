#include "common/log.hpp"

#include <cstdio>
#include <ctime>
#include <mutex>

#include "common/timing.hpp"

namespace brim::log {

namespace {
std::mutex g_mu;
Level g_level = Level::Info;
const char* g_proc = "brim";
const char* level_name(Level l) {
  switch (l) {
    case Level::Debug: return "DEBUG";
    case Level::Info: return "INFO ";
    case Level::Warn: return "WARN ";
    case Level::Error: return "ERROR";
  }
  return "?";
}
}  // namespace

void set_level(Level l) { g_level = l; }
void set_process_name(const char* name) { g_proc = name; }

void write(Level l, const char* component, const char* fmt, ...) {
  if (l < g_level) return;
  char msg[1024];
  va_list ap;
  va_start(ap, fmt);
  vsnprintf(msg, sizeof msg, fmt, ap);
  va_end(ap);

  timespec wall{};
  clock_gettime(CLOCK_REALTIME, &wall);
  tm t{};
  gmtime_r(&wall.tv_sec, &t);
  char ts[32];
  strftime(ts, sizeof ts, "%H:%M:%S", &t);

  std::lock_guard<std::mutex> lk(g_mu);
  std::fprintf(stderr, "%s.%03ldZ mono=%lld %s %s[%s] %s\n", ts, wall.tv_nsec / 1000000L,
               static_cast<long long>(timing::mono_ms()), level_name(l), g_proc, component, msg);
  std::fflush(stderr);
}

}  // namespace brim::log
