#include "dashboard/dashboard.hpp"

#include <fstream>

#include "common/log.hpp"

namespace brim::dashboard {

namespace {
class LogDashboard final : public Dashboard {
 public:
  explicit LogDashboard(std::string run_dir) : run_dir_(std::move(run_dir)) {}
  void show_pairing(const std::string& pin, const std::string& url) override {
    BRIM_LOGI("dashboard", "==============================================");
    BRIM_LOGI("dashboard", "  PAIRING PIN: %s   open %s on your phone", pin.c_str(), url.c_str());
    BRIM_LOGI("dashboard", "==============================================");
    std::ofstream f(run_dir_ + "/pairing.txt", std::ios::trunc);
    if (f) f << pin << "\n" << url << "\n";
  }
  void show_status(const std::string& line) override { BRIM_LOGI("dashboard", "%s", line.c_str()); }

 private:
  std::string run_dir_;
};
}  // namespace

std::unique_ptr<Dashboard> make_log_dashboard(const std::string& run_dir) {
  return std::make_unique<LogDashboard>(run_dir);
}

}  // namespace brim::dashboard
