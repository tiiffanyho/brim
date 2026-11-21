// A supervised child process: spawn with posix_spawn (POSIX, available on QNX), optional
// scheduling priority, non-blocking exit check, hard kill.
#pragma once

#include <sys/types.h>

#include <string>
#include <vector>

namespace brim::supervisor {

class Child {
 public:
  Child(std::string name, std::string path, std::vector<std::string> args, int priority)
      : name_(std::move(name)), path_(std::move(path)), args_(std::move(args)), priority_(priority) {}

  bool spawn();
  bool running();           // reaps if exited (waitpid WNOHANG)
  void kill_hard();         // SIGKILL + reap
  void terminate(int wait_ms);  // SIGTERM, then SIGKILL after wait_ms
  pid_t pid() const { return pid_; }
  const std::string& name() const { return name_; }
  int last_status() const { return last_status_; }

 private:
  std::string name_, path_;
  std::vector<std::string> args_;
  int priority_;  // QNX thread priority (1..63); 0 = inherit. Ignored on the laptop.
  pid_t pid_ = -1;
  int last_status_ = 0;
};

}  // namespace brim::supervisor
