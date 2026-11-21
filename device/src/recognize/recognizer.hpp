// Recognition: face crop -> embedding ("fingerprint") -> enrolled person or anonymous stranger.
// Privacy rules enforced here:
//   * only enrolled people (in the Gallery) ever get a name;
//   * strangers get a short-lived anon id ("stranger-3") and are forgotten after 30 minutes;
//   * forget(person_id) drops the embedding from memory immediately (app_server deletes the files).
#pragma once

#include <array>
#include <cstdint>
#include <map>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <vector>

#include "capture/camera.hpp"
#include "faces/face_detector.hpp"

namespace brim::recognize {

constexpr int kEmbeddingDim = 128;
using Embedding = std::array<float, kEmbeddingDim>;

float cosine_similarity(const Embedding& a, const Embedding& b);

class Embedder {
 public:
  virtual ~Embedder() = default;
  virtual bool load() = 0;
  virtual std::optional<Embedding> embed(const capture::Frame& frame, const faces::FaceBox& box) = 0;
};

std::unique_ptr<Embedder> make_stub_embedder();
// TODO(ai): face embedding model (e.g. MobileFaceNet / FaceNet-128 .tflite) on tflite-runtime 2.21.0.
//   Align with landmarks (OpenCV warpAffine), 112x112 input, L2-normalize output.
std::unique_ptr<Embedder> make_tflite_embedder(const std::string& models_dir);

struct Match {
  std::int64_t person_id = 0;
  std::string display_name;
  float score = 0;
};

// Enrolled people, in memory. Loaded at startup from data/embeddings/<person_id>.bin.
class Gallery {
 public:
  void add(std::int64_t person_id, std::string name, Embedding e);
  bool forget(std::int64_t person_id);
  void rename(std::int64_t person_id, std::string name);
  std::optional<Match> best_match(const Embedding& e, float min_score) const;
  std::size_t size() const;
  // TODO(ai): load_from_dir(data_dir) reading <id>.bin (128 float32 LE) + names from SQLite
  //   (read-only open of data/brim.db, or ask app_server over the bus at startup).

 private:
  struct Entry {
    std::string name;
    Embedding e;
  };
  mutable std::mutex mu_;
  std::map<std::int64_t, Entry> people_;
};

// Anonymous strangers: assigns "stranger-N" ids to unmatched embeddings and forgets them
// `forget_after_ms` after they were last seen. Nothing about strangers is ever written to disk
// by brim; app_server purges stranger sightings older than 30 min from SQLite too.
class StrangerTracker {
 public:
  explicit StrangerTracker(std::int64_t forget_after_ms = 30 * 60 * 1000, float same_person_score = 0.75f)
      : forget_after_ms_(forget_after_ms), same_score_(same_person_score) {}
  std::string observe(const Embedding& e, std::int64_t now_ms);
  void expire(std::int64_t now_ms);
  std::size_t size() const { return strangers_.size(); }

 private:
  struct Stranger {
    std::string anon_id;
    Embedding e;
    std::int64_t last_seen_ms;
  };
  std::int64_t forget_after_ms_;
  float same_score_;
  std::vector<Stranger> strangers_;
  std::uint64_t next_id_ = 1;
};

}  // namespace brim::recognize
