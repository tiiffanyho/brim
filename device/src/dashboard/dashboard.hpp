// The "dashboard": the HDMI monitor used for the demo (pairing PIN, IP, status). There is no
// display on the hat itself — the wearer hears the pairing PIN in the earpiece, because app_server
// sends brim a speak_pairing_pin command when it creates the PIN.
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
