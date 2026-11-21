#include "supervisor/watchdog.hpp"

#include "common/log.hpp"

namespace brim::supervisor {

namespace {
class SoftwareWatchdog final : public Watchdog {
 public:
  bool start(int timeout_ms) override {
    BRIM_LOGI("watchdog", "software watchdog (no-op), timeout %d ms", timeout_ms);
    return true;
  }
  void kick() override {}
  const char* name() const override { return "software"; }
};
}  // namespace

std::unique_ptr<Watchdog> make_software_watchdog() { return std::make_unique<SoftwareWatchdog>(); }

std::unique_ptr<Watchdog> make_hardware_watchdog() {
  BRIM_LOGW("watchdog", "hardware watchdog not implemented yet; using software watchdog");  // TODO(hw)
  return make_software_watchdog();
}

}  // namespace brim::supervisor
