#include "cues/cue_engine.hpp"

#include "common/log.hpp"

namespace brim::cues {

namespace {
class StubCueEngine final : public CueEngine {
 public:
  std::vector<schema::CueEvent> update(const capture::Frame&, const std::vector<TrackedPerson>&) override { return {}; }
};
}  // namespace

std::unique_ptr<CueEngine> make_stub_cue_engine() { return std::make_unique<StubCueEngine>(); }
std::unique_ptr<CueEngine> make_mediapipe_cue_engine(const config::CueThresholds&) {
  BRIM_LOGW("cues", "MediaPipe cue engine not implemented yet; using stub");  // TODO(ai)
  return make_stub_cue_engine();
}

bool NameAnnouncer::should_announce(std::int64_t person_id, std::int64_t now_ms) {
  auto it = last_.find(person_id);
  if (it != last_.end() && now_ms - it->second < repeat_after_ms_) return false;
  last_[person_id] = now_ms;
  return true;
}

}  // namespace brim::cues
