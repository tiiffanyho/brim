#include "audio/speak_queue.hpp"

namespace brim::audio {

const char* to_string(Priority p) {
  switch (p) {
    case Priority::Safety: return "safety";
    case Priority::Nav: return "nav";
    case Priority::Info: return "info";
  }
  return "?";
}

void SpeakQueue::set_config(SpeakQueueConfig cfg) {
  std::lock_guard<std::mutex> lk(mu_);
  cfg_ = cfg;
}

std::size_t SpeakQueue::size() const {
  std::lock_guard<std::mutex> lk(mu_);
  return q_[0].size() + q_[1].size() + q_[2].size();
}

bool SpeakQueue::push(SpeechItem item) {
  std::lock_guard<std::mutex> lk(mu_);
  const int p = static_cast<int>(item.priority);
  std::size_t total = q_[0].size() + q_[1].size() + q_[2].size();
  if (total >= cfg_.capacity) {
    // Evict the oldest item from the lowest priority level that is <= the new item's urgency.
    bool made_room = false;
    for (int lvl = 2; lvl >= p; --lvl) {
      if (!q_[lvl].empty()) {
        q_[lvl].pop_front();
        evicted_++;
        made_room = true;
        break;
      }
    }
    if (!made_room) return false;
  }
  q_[p].push_back(std::move(item));
  return true;
}

void SpeakQueue::drop_stale_info(std::int64_t now_ms) {
  auto& info = q_[static_cast<int>(Priority::Info)];
  while (!info.empty() && now_ms - info.front().created_ms > cfg_.info_max_age_ms) {
    info.pop_front();
    dropped_stale_++;
  }
}

std::optional<SpeechItem> SpeakQueue::pop_next(std::int64_t now_ms, bool someone_talking) {
  std::lock_guard<std::mutex> lk(mu_);
  drop_stale_info(now_ms);

  auto take = [](std::deque<SpeechItem>& d) {
    SpeechItem it = std::move(d.front());
    d.pop_front();
    return it;
  };

  if (!q_[0].empty()) return take(q_[0]);  // safety: always, even over talking
  if (!q_[1].empty()) return take(q_[1]);  // navigation: right after the warning that caused it
  auto& info = q_[2];
  if (info.empty() || someone_talking) return std::nullopt;
  if (last_info_spoken_ms_ && now_ms - *last_info_spoken_ms_ < cfg_.info_min_interval_ms) return std::nullopt;
  last_info_spoken_ms_ = now_ms;
  return take(info);
}

}  // namespace brim::audio
