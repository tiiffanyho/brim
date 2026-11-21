#include "common/log.hpp"
#include "common/timing.hpp"
#include "ipc/bus.hpp"

namespace brim::ipc {

EventBus::EventBus(std::string channel, std::size_t capacity)
    : channel_(std::move(channel)),
      capacity_(capacity),
      conn_(connect(channel_)),
      pulse_conn_(connect(channel_)),
      worker_([this] { run(); }) {}

EventBus::~EventBus() {
  {
    std::lock_guard<std::mutex> lk(mu_);
    stop_ = true;
  }
  cv_.notify_all();
  if (worker_.joinable()) worker_.join();
}

bool EventBus::publish(std::string json) {
  bool dropped = false;
  {
    std::lock_guard<std::mutex> lk(mu_);
    if (q_.size() >= capacity_) {
      q_.pop_front();
      dropped = true;
      dropped_++;
    }
    q_.push_back(std::move(json));
  }
  cv_.notify_one();
  return !dropped;
}

bool EventBus::pulse(int code, int value) {
  std::lock_guard<std::mutex> lk(pulse_mu_);
  return pulse_conn_->pulse(code, value);
}

std::size_t EventBus::queued() const {
  std::lock_guard<std::mutex> lk(mu_);
  return q_.size();
}

void EventBus::run() {
  std::int64_t last_warn_ms = 0;
  for (;;) {
    std::string msg;
    {
      std::unique_lock<std::mutex> lk(mu_);
      cv_.wait(lk, [this] { return stop_ || !q_.empty(); });
      if (stop_ && q_.empty()) return;
      msg = std::move(q_.front());
      q_.pop_front();
    }
    if (conn_->send(msg)) {
      sent_++;
    } else {
      dropped_++;
      // Receiver down (e.g. app_server restarting). Rate-limit the warning.
      auto now = timing::mono_ms();
      if (now - last_warn_ms > 5000) {
        BRIM_LOGW("bus", "channel %s unreachable, dropping events (dropped=%llu)", channel_.c_str(),
                  static_cast<unsigned long long>(dropped_.load()));
        last_warn_ms = now;
      }
    }
  }
}

}  // namespace brim::ipc
