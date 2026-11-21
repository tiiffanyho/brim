// The "dashboard": the HDMI monitor used for the demo (pairing PIN, IP, status). There is no
// display on the hat itself and the Pi plays no sound, so this is the only place the pairing PIN
// appears: a helper reads it to the wearer or types it into their phone. It is never sent to
// phones over the event stream.
#pragma once

#include <memory>
#include <string>

namespace brim::dashboard {

class Dashboard {
 public:
  virtual ~Dashboard() = default;
  virtual void show_pairing(const std::string& pin, const std::string& url) = 0;
  virtual void show_status(const std::string& line) = 0;
};

// Prints to the log and writes <run_dir>/pairing.txt.
std::unique_ptr<Dashboard> make_log_dashboard(const std::string& run_dir);
// TODO(hw): draw the demo dashboard on the HDMI monitor via QNX Screen (libscreen).
//   TODO(qnx-check): confirm which display stack the Pi 5 QNX image provides (ls /dev/screen, and
//   whether the BSP starts the `screen` service).

}  // namespace brim::dashboard
