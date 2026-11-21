// The hat's "dashboard": what a sighted helper sees during setup (pairing PIN, IP, status).
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
// TODO(hw): small display on the hat (e.g. SPI OLED) or the Pi's HDMI via QNX Screen
//   (libscreen). TODO(qnx-check): confirm which display stack the Pi 5 QNX image provides.
//   Could also speak the PIN through the earpiece for a blind user pairing on their own.

}  // namespace brim::dashboard
