#include "recognize/recognizer.hpp"

#include <cmath>

#include "common/log.hpp"

namespace brim::recognize {

float cosine_similarity(const Embedding& a, const Embedding& b) {
  double dot = 0, na = 0, nb = 0;
  for (int i = 0; i < kEmbeddingDim; ++i) {
    dot += double(a[i]) * b[i];
    na += double(a[i]) * a[i];
    nb += double(b[i]) * b[i];
  }
  if (na == 0 || nb == 0) return 0;
  return static_cast<float>(dot / (std::sqrt(na) * std::sqrt(nb)));
}

namespace {
class StubEmbedder final : public Embedder {
 public:
  bool load() override { return true; }
  std::optional<Embedding> embed(const capture::Frame&, const faces::FaceBox&) override { return std::nullopt; }
};
}  // namespace

std::unique_ptr<Embedder> make_stub_embedder() { return std::make_unique<StubEmbedder>(); }
std::unique_ptr<Embedder> make_tflite_embedder(const std::string& models_dir) {
  BRIM_LOGW("recognize", "tflite embedder not implemented yet (models_dir=%s); using stub", models_dir.c_str());  // TODO(ai)
  return make_stub_embedder();
}

void Gallery::add(std::int64_t id, std::string name, Embedding e) {
  std::lock_guard<std::mutex> lk(mu_);
  people_[id] = Entry{std::move(name), e};
}
bool Gallery::forget(std::int64_t id) {
  std::lock_guard<std::mutex> lk(mu_);
  return people_.erase(id) > 0;
}
void Gallery::rename(std::int64_t id, std::string name) {
  std::lock_guard<std::mutex> lk(mu_);
  auto it = people_.find(id);
  if (it != people_.end()) it->second.name = std::move(name);
}
std::size_t Gallery::size() const {
  std::lock_guard<std::mutex> lk(mu_);
  return people_.size();
}
std::optional<Match> Gallery::best_match(const Embedding& e, float min_score) const {
  std::lock_guard<std::mutex> lk(mu_);
  std::optional<Match> best;
  for (const auto& [id, entry] : people_) {
    float s = cosine_similarity(e, entry.e);
    if (s >= min_score && (!best || s > best->score)) best = Match{id, entry.name, s};
  }
  return best;
}

std::string StrangerTracker::observe(const Embedding& e, std::int64_t now_ms) {
  expire(now_ms);
  for (auto& s : strangers_) {
    if (cosine_similarity(e, s.e) >= same_score_) {
      s.last_seen_ms = now_ms;
      return s.anon_id;
    }
  }
  strangers_.push_back(Stranger{"stranger-" + std::to_string(next_id_++), e, now_ms});
  return strangers_.back().anon_id;
}

void StrangerTracker::expire(std::int64_t now_ms) {
  for (auto it = strangers_.begin(); it != strangers_.end();) {
    if (now_ms - it->last_seen_ms >= forget_after_ms_) it = strangers_.erase(it);
    else ++it;
  }
}

}  // namespace brim::recognize
