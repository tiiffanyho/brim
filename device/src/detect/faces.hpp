// Face recognition of ENROLLED people only. Agreed by the team (Gloriana, Tiffany, Kim) on
// branch feat/face-recognition; see CLAUDE.md for the rules this follows:
//   * only people who enrolled (scripts/faces/enrol.py) are ever named; everyone else stays
//     "Person", and a weak or ambiguous match is "unknown", never a guess;
//   * enrolled embeddings live only on the hat in config faces_dir (git-ignored); no photo is kept,
//     scripts/faces/remove.py deletes a person, and the gallery is reloaded so removal takes effect;
//   * nothing runs while nobody is enrolled: no face model is loaded or run;
//   * faces of people who are not enrolled are embedded in memory only for the comparison and
//     dropped at once; no frame, face, embedding or name is ever written to disk or the log;
//   * recognition runs on its own low-priority thread, at most once per person track per
//     check_interval_ms, after the frame's warnings are queued: it never delays a safety alert,
//     and a warning only uses a name that is already known.
#pragma once

#include <atomic>
#include <condition_variable>
#include <cstdint>
#include <map>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <thread>
#include <vector>

#include "capture/camera.hpp"
#include "common/config.hpp"
#include "detect/detector.hpp"

namespace brim::faces {

// A name we may say: 1-32 characters, letters first, then letters, spaces, hyphens or apostrophes.
bool valid_name(const std::string& name);

// "Person ahead, move left" + "Tiffany" -> "Tiffany ahead, move left". Only a phrase that starts
// with "Person ahead" is ever named; anything else is returned unchanged.
std::string named_text(const std::string& text, const std::string& name);

struct Match {
  std::string name;  // empty = unknown
  double score = 0;  // best cosine similarity
  double margin = 0; // best minus the next enrolled person's best
};

// The enrolled people: name -> L2-normalised embeddings. Pure logic, unit-tested.
class Gallery {
 public:
  // Reads <dir>/<name>/embeddings.json for each folder. Folders with an invalid name, a different
  // embedder_id, a wrong dimension or broken JSON are skipped (and logged, without the name).
  static Gallery load(const std::string& dir, const std::string& embedder_id);
  void add(const std::string& name, std::vector<float> embedding);  // normalises
  bool empty() const { return people_.empty(); }
  std::size_t people() const { return people_.size(); }
  bool has(const std::string& name) const { return people_.count(name) != 0; }
  int dim() const { return dim_; }
  // The best enrolled person if score >= threshold and it beats the next person by min_margin.
  Match match(const std::vector<float>& embedding, double threshold, double min_margin) const;

 private:
  std::map<std::string, std::vector<std::vector<float>>> people_;
  int dim_ = 0;
};

// Track id -> name, forgotten ttl_ms after the last match. In memory only.
class NameCache {
 public:
  explicit NameCache(std::int64_t ttl_ms) : ttl_ms_(ttl_ms) {}
  void set(int track_id, const std::string& name, std::int64_t now_ms);
  void forget(int track_id) { names_.erase(track_id); }
  std::string get(int track_id, std::int64_t now_ms) const;  // "" = no name
  void expire(std::int64_t now_ms);
  void keep_only(const Gallery& g);  // after a reload: removed people are never said again
  void clear() { names_.clear(); }
  std::size_t size() const { return names_.size(); }

 private:
  struct Entry {
    std::string name;
    std::int64_t until_ms;
  };
  std::int64_t ttl_ms_;
  std::map<int, Entry> names_;
};

// Finds the face inside a person box and returns its embedding. The real one uses OpenCV 4.12
// (oss.qnx.com) FaceDetectorYN (YuNet) + FaceRecognizerSF (SFace); see models/README.md.
class FaceEmbedder {
 public:
  virtual ~FaceEmbedder() = default;
  virtual bool load() = 0;  // false = unavailable: nobody is ever named
  // false = no usable face in the box (too small, unsure, turned away).
  virtual bool embed(const capture::Frame& frame, const detect::Box& person, std::vector<float>& out) = 0;
  virtual const char* name() const = 0;
};
// Built with BRIM_WITH_OPENCV (on by default for QNX). Otherwise an embedder whose load() fails.
std::unique_ptr<FaceEmbedder> make_face_embedder(const config::FaceConfig& cfg, const std::string& models_dir);
bool opencv_available();

// The pipeline's view: offer person boxes after the frame's cues are queued, ask for known names.
class Recogniser {
 public:
  Recogniser(config::FaceConfig cfg, std::unique_ptr<FaceEmbedder> embedder);
  ~Recogniser();
  Recogniser(const Recogniser&) = delete;
  Recogniser& operator=(const Recogniser&) = delete;

  void start();
  void stop();
  // Non-blocking. Copies the frame only if the worker is idle, somebody is enrolled and this
  // track was not checked within check_interval_ms. Never waits.
  void offer(const capture::Frame& frame, const detect::Detection& person, std::int64_t now_ms);
  // The name for this track if already known and fresh, else "".
  std::string name_for(int track_id, std::int64_t now_ms);
  // For the health event: "off (nobody enrolled)", "3 enrolled", "unavailable (...)".
  std::string status() const;
  double last_ms() const { return last_ms_; }

 private:
  void run();
  void reload(std::int64_t now_ms);

  config::FaceConfig cfg_;
  std::unique_ptr<FaceEmbedder> embedder_;
  bool embedder_loaded_ = false;
  std::atomic<bool> embedder_failed_{false};
  mutable std::mutex mu_;
  std::condition_variable cv_;
  Gallery gallery_;
  NameCache cache_;
  std::map<int, std::int64_t> last_check_;
  // Single job slot, reused: no per-frame allocation once it has grown to the frame size.
  capture::Frame job_frame_;
  detect::Detection job_person_;
  std::int64_t job_now_ = 0;
  bool job_ready_ = false;
  bool busy_ = false;
  std::atomic<bool> stop_{false};
  std::atomic<std::size_t> enrolled_{0};
  std::atomic<double> last_ms_{0};
  std::int64_t last_reload_ms_ = -1;
  std::thread worker_;
};

}  // namespace brim::faces
