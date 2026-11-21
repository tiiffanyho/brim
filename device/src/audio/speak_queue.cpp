#include "audio/speak_queue.hpp"

namespace brim::audio {

const char* to_string(Priority p) {
  switch (p) {
    case Priority::Safety: return "safety";
    case Priority::Name: return "name";
    case Priority::Cue: return "cue";
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

void SpeakQueue::drop_stale_cues(std::int64_t now_ms) {
  auto& cues = q_[static_cast<int>(Priority::Cue)];
  while (!cues.empty() && now_ms - cues.front().created_ms > cfg_.cue_max_age_ms) {
    cues.pop_front();
    dropped_stale_++;
  }
}

std::optional<SpeechItem> SpeakQueue::pop_next(std::int64_t now_ms, bool someone_talking) {
  std::lock_guard<std::mutex> lk(mu_);
  drop_stale_cues(now_ms);

  auto take = [](std::deque<SpeechItem>& d) {
    SpeechItem it = std::move(d.front());
    d.pop_front();
    return it;
  };

  if (!q_[0].empty()) return take(q_[0]);  // safety: always, even over talking
  if (!q_[1].empty()) return take(q_[1]);  // names
  auto& cues = q_[2];
  if (cues.empty() || someone_talking) return std::nullopt;
  if (last_cue_spoken_ms_ && now_ms - *last_cue_spoken_ms_ < cfg_.cue_min_interval_ms) return std::nullopt;
  last_cue_spoken_ms_ = now_ms;
  return take(cues);
}

}  // namespace brim::audio
