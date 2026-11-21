// Shared runtime state between the relay thread (bus -> DB/broadcast) and the HTTP thread.
#pragma once

#include <atomic>
#include <cstdint>
#include <mutex>
#include <optional>

#include "common/timing.hpp"
#include "events.gen.hpp"

namespace brim::app {

struct ServerState {
  std::int64_t start_mono_ms = timing::mono_ms();
  std::atomic<int> ws_clients{0};
  std::atomic<std::int64_t> last_brim_event_mono_ms{0};
  std::atomic<std::int64_t> restart_count{0};
  std::atomic<std::uint64_t> relayed{0};
  std::atomic<std::uint64_t> rejected{0};

  void set_health(const schema::HealthEvent& h) {
    std::lock_guard<std::mutex> lk(mu_);
    last_health_ = h;
  }
  std::optional<schema::HealthEvent> health() const {
    std::lock_guard<std::mutex> lk(mu_);
    return last_health_;
  }
  void add_relay_latency(double ms) {
    std::lock_guard<std::mutex> lk(mu_);
    relay_latency_ms_ = relay_latency_ms_ ? *relay_latency_ms_ * 0.9 + ms * 0.1 : ms;
  }
  std::optional<double> relay_latency_ms() const {
    std::lock_guard<std::mutex> lk(mu_);
    return relay_latency_ms_;
  }

 private:
  mutable std::mutex mu_;
  std::optional<schema::HealthEvent> last_health_;
  std::optional<double> relay_latency_ms_;
};

}  // namespace brim::app
