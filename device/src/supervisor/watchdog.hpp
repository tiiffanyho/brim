// Watchdog behind an interface. If the supervisor itself dies or hangs, the hardware watchdog
// reboots the Pi (the boot script restarts supervisor -> brim): the last line of "cannot fail".
#pragma once

#include <memory>

namespace brim::supervisor {

class Watchdog {
 public:
  virtual ~Watchdog() = default;
  virtual bool start(int timeout_ms) = 0;
  virtual void kick() = 0;
  virtual const char* name() const = 0;
};

// Does nothing but log at start. Used on the laptop and until the hardware one is wired up.
std::unique_ptr<Watchdog> make_software_watchdog();

// TODO(hw)/TODO(qnx-check): Pi 5 (BCM2712) hardware watchdog under QNX 8.0.
//   Many QNX BSPs start a `wdtkick` utility from the IFS build file that kicks the watchdog on its
//   own — check the Pi 5 BSP's build file and `pidin | grep wdt` on the Pi. If it exists, either
//   stop it and kick from here (so a hung supervisor really triggers a reset), or make wdtkick
//   conditional on a file this process touches. Look for the PM/watchdog registers in the BSP
//   docs; access via mmap_device_io()/mmap_device_memory() (needs PROCMGR_AID_IO ability).
std::unique_ptr<Watchdog> make_hardware_watchdog();

}  // namespace brim::supervisor
