#include "supervisor/child.hpp"

#include <sched.h>
#include <signal.h>
#include <spawn.h>
#include <sys/wait.h>
#include <unistd.h>

#include <cerrno>
#include <cstring>

#include "common/log.hpp"
#include "common/timing.hpp"

extern char** environ;

namespace brim::supervisor {

bool Child::spawn() {
  std::vector<char*> argv;
  argv.push_back(const_cast<char*>(path_.c_str()));
  for (auto& a : args_) argv.push_back(const_cast<char*>(a.c_str()));
  argv.push_back(nullptr);

  posix_spawnattr_t attr;
  posix_spawnattr_init(&attr);
  short flags = 0;
#if defined(__QNX__)
  // Real-time priority for brim, low for app_server. QNX: 10 is the default user priority.
  // TODO(qnx-check): confirm SCHED_RR + these priorities with `pidin -p brim` on the Pi; the
  //   supervisor needs PROCMGR_AID_PRIORITY to raise above 63/root limits if run as non-root.
  if (priority_ > 0) {
    sched_param sp{};
    sp.sched_priority = priority_;
    posix_spawnattr_setschedpolicy(&attr, SCHED_RR);
    posix_spawnattr_setschedparam(&attr, &sp);
    flags |= POSIX_SPAWN_SETSCHEDULER | POSIX_SPAWN_SETSCHEDPARAM;
  }
#else
  (void)priority_;  // laptop: run at normal priority
#endif
  // New process group so Ctrl-C in the dev terminal reaches the supervisor first, which then
  // stops children in order.
  flags |= POSIX_SPAWN_SETPGROUP;
  posix_spawnattr_setpgroup(&attr, 0);
  posix_spawnattr_setflags(&attr, flags);

  pid_t pid = -1;
  int rc = posix_spawn(&pid, path_.c_str(), nullptr, &attr, argv.data(), environ);
  posix_spawnattr_destroy(&attr);
  if (rc != 0) {
    BRIM_LOGE("supervisor", "spawn %s (%s) failed: %s", name_.c_str(), path_.c_str(), std::strerror(rc));
    pid_ = -1;
    return false;
  }
  pid_ = pid;
  BRIM_LOGI("supervisor", "started %s pid=%d", name_.c_str(), static_cast<int>(pid));
  return true;
}

bool Child::running() {
  if (pid_ <= 0) return false;
  int status = 0;
  pid_t r = waitpid(pid_, &status, WNOHANG);
  if (r == 0) return true;
  if (r == pid_) {
    last_status_ = status;
    if (WIFSIGNALED(status)) BRIM_LOGW("supervisor", "%s pid=%d killed by signal %d", name_.c_str(), (int)pid_, WTERMSIG(status));
    else BRIM_LOGW("supervisor", "%s pid=%d exited with %d", name_.c_str(), (int)pid_, WEXITSTATUS(status));
  }
  pid_ = -1;
  return false;
}

void Child::kill_hard() {
  if (pid_ <= 0) return;
  ::kill(pid_, SIGKILL);
  int status = 0;
  waitpid(pid_, &status, 0);
  last_status_ = status;
  pid_ = -1;
}

void Child::terminate(int wait_ms) {
  if (pid_ <= 0) return;
  ::kill(pid_, SIGTERM);
  auto deadline = timing::mono_ms() + wait_ms;
  while (timing::mono_ms() < deadline) {
    if (!running()) return;
    timing::sleep_ms(20);
  }
  kill_hard();
}

}  // namespace brim::supervisor
