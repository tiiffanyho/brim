// supervisor: keeps brim (and optionally app_server) alive.
//
//   * brim pulses a heartbeat every 200 ms (only while its pipeline is making progress).
//   * Every 500 ms we check: brim exited?  -> restart.  No heartbeat for 1000 ms? -> SIGKILL + restart.
//     Worst case detection 1000 + 500 ms, respawn is immediate => back within 2 s.
//   * Each restart is logged and published as a `fault` event (action=restarting) to app_server,
//     then `recovered` once heartbeats resume.
//   * app_server (lowest priority) is restarted if it exits; it has no heartbeat.
//   * The watchdog is kicked from this loop, so if the supervisor hangs the hardware resets the Pi.
//
// usage: supervisor --brim PATH [--brim-arg ARG]... [--app-server PATH [--app-server-arg ARG]...]
//                   [--heartbeat-timeout-ms 1000] [--check-ms 500] [--hw-watchdog]
#include <csignal>
#include <cstdlib>
#include <cstring>
#include <memory>

#include "common/events.hpp"
#include "common/log.hpp"
#include "common/timing.hpp"
#include "ipc/bus.hpp"
#include "supervisor/child.hpp"
#include "supervisor/watchdog.hpp"

using namespace brim;
namespace s = brim::schema;

namespace {
std::atomic<bool> g_stop{false};
void on_signal(int) { g_stop = true; }

void usage() {
  std::fprintf(stderr,
               "usage: supervisor --brim PATH [--brim-arg ARG]... [--app-server PATH [--app-server-arg ARG]...]\n"
               "                  [--heartbeat-timeout-ms N] [--check-ms N] [--hw-watchdog]\n");
}
}  // namespace

int main(int argc, char** argv) {
  log::set_process_name("supervisor");
  std::string brim_path, app_path;
  std::vector<std::string> brim_args, app_args;
  int hb_timeout_ms = 1000, check_ms = 500;
  bool hw_watchdog = false;
  for (int i = 1; i < argc; ++i) {
    auto next = [&](const char* flag) -> const char* {
      if (i + 1 >= argc) {
        std::fprintf(stderr, "%s needs a value\n", flag);
        std::exit(2);
      }
      return argv[++i];
    };
    if (!std::strcmp(argv[i], "--brim")) brim_path = next("--brim");
    else if (!std::strcmp(argv[i], "--brim-arg")) brim_args.push_back(next("--brim-arg"));
    else if (!std::strcmp(argv[i], "--app-server")) app_path = next("--app-server");
    else if (!std::strcmp(argv[i], "--app-server-arg")) app_args.push_back(next("--app-server-arg"));
    else if (!std::strcmp(argv[i], "--heartbeat-timeout-ms")) hb_timeout_ms = std::atoi(next("--heartbeat-timeout-ms"));
    else if (!std::strcmp(argv[i], "--check-ms")) check_ms = std::atoi(next("--check-ms"));
    else if (!std::strcmp(argv[i], "--hw-watchdog")) hw_watchdog = true;
    else {
      usage();
      return 2;
    }
  }
  if (brim_path.empty()) {
    usage();
    return 2;
  }
  std::signal(SIGINT, on_signal);
  std::signal(SIGTERM, on_signal);
  // The supervisor must not die from stray signals meant for brim (e.g. a demo `kill -USR1`).
  std::signal(SIGUSR1, SIG_IGN);
  std::signal(SIGUSR2, SIG_IGN);
  std::signal(SIGHUP, SIG_IGN);
  std::signal(SIGPIPE, SIG_IGN);

  std::string err;
  auto hb = ipc::listen(events::kSupervisorChannel, &err);
  if (!hb) {
    // Cannot-fail: still keep brim alive (restart on exit), just without hang detection.
    BRIM_LOGE("supervisor", "cannot listen on %s: %s -> heartbeat checks DISABLED, exit-only monitoring",
              events::kSupervisorChannel, err.c_str());
  }
  ipc::EventBus faults(events::kAppServerChannel, 64);
  events::EventStamper stamper("supervisor");
  auto fault = [&](s::FaultAction action, const std::string& component, const std::string& msg, std::int64_t restarts) {
    s::FaultEvent f;
    f.component = component;
    f.action = action;
    f.message = msg;
    f.restart_count = restarts;
    faults.publish(stamper.stamp(f));
  };

  auto wd = hw_watchdog ? supervisor::make_hardware_watchdog() : supervisor::make_software_watchdog();
  wd->start(5000);

  // QNX priorities: brim above default (10), app_server below it.
  supervisor::Child brim("brim", brim_path, brim_args, 20);
  std::unique_ptr<supervisor::Child> app;
  if (!app_path.empty()) app = std::make_unique<supervisor::Child>("app_server", app_path, app_args, 8);

  std::int64_t restarts = 0;
  std::int64_t last_hb_ms = 0;
  bool awaiting_recovery = false;
  constexpr int kStartupGraceMs = 1000;  // time for a fresh brim to send its first heartbeat

  auto start_brim = [&] {
    brim.spawn();
    last_hb_ms = timing::mono_ms() + kStartupGraceMs;
  };
  if (app) app->spawn();
  start_brim();

  std::int64_t next_check = timing::mono_ms() + check_ms;
  while (!g_stop) {
    int wait = static_cast<int>(next_check - timing::mono_ms());
    if (wait > 0 && !hb) {
      timing::sleep_ms(wait);
      continue;
    }
    if (wait > 0) {
      auto m = hb->receive(wait);
      if (m && m->kind == ipc::Message::Kind::Pulse && m->code == events::kPulseHeartbeat) {
        last_hb_ms = timing::mono_ms();
        if (awaiting_recovery) {
          awaiting_recovery = false;
          BRIM_LOGI("supervisor", "brim recovered (heartbeat resumed)");
          fault(s::FaultAction::Recovered, "brim", "heartbeat resumed", restarts);
        }
      }
      continue;
    }
    next_check += check_ms;
    const std::int64_t now = timing::mono_ms();
    wd->kick();

    std::string reason;
    if (!brim.running()) {
      reason = "process exited";
    } else if (hb && now - last_hb_ms > hb_timeout_ms) {
      reason = "heartbeat missed for " + std::to_string(now - last_hb_ms) + " ms";
      brim.kill_hard();
    }
    if (!reason.empty()) {
      restarts++;
      BRIM_LOGW("supervisor", "restarting brim (#%lld): %s", static_cast<long long>(restarts), reason.c_str());
      fault(s::FaultAction::Restarting, "brim", reason, restarts);
      std::int64_t t0 = timing::mono_ms();
      start_brim();
      awaiting_recovery = true;
      BRIM_LOGI("supervisor", "brim respawned in %lld ms", static_cast<long long>(timing::mono_ms() - t0));
    }
    if (app && !app->running()) {
      BRIM_LOGW("supervisor", "restarting app_server");
      app->spawn();  // its fault event would have nowhere to go until it is up; log only
    }
  }

  BRIM_LOGI("supervisor", "stopping children");
  brim.terminate(1000);
  if (app) app->terminate(1000);
  return 0;
}
