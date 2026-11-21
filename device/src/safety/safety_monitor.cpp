#include "safety/safety_monitor.hpp"

#include "common/log.hpp"

namespace brim::safety {

namespace {
class StubSafetyMonitor final : public SafetyMonitor {
 public:
  bool healthy() const override { return true; }
  std::vector<schema::SafetyAlertEvent> check(const capture::Frame&, const std::vector<detect::Detection>&) override {
    return {};
  }
};
}  // namespace

std::unique_ptr<SafetyMonitor> make_stub_safety_monitor() { return std::make_unique<StubSafetyMonitor>(); }

std::unique_ptr<SafetyMonitor> make_vision_safety_monitor(const config::SafetyThresholds&, const config::NavThresholds&) {
  BRIM_LOGW("safety", "vision safety monitor not implemented yet; using stub");  // TODO(ai)
  return make_stub_safety_monitor();
}

}  // namespace brim::safety
