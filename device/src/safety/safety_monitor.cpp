#include "safety/safety_monitor.hpp"

#include "common/log.hpp"

namespace brim::safety {

namespace {
class StubSafetyMonitor final : public SafetyMonitor {
 public:
  bool healthy() const override { return true; }
  std::vector<schema::SafetyAlertEvent> check(const capture::Frame&) override { return {}; }
};
}  // namespace

std::unique_ptr<SafetyMonitor> make_stub_safety_monitor() { return std::make_unique<StubSafetyMonitor>(); }
std::unique_ptr<SafetyMonitor> make_sensor_safety_monitor(const config::CueThresholds&) {
  BRIM_LOGW("safety", "safety sensor not implemented yet; using stub");  // TODO(hw)
  return make_stub_safety_monitor();
}

}  // namespace brim::safety
