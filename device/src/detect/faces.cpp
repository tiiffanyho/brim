#include "detect/faces.hpp"

#include <dirent.h>

#include <algorithm>
#include <cctype>
#include <cmath>

#include "common/json.hpp"
#include "common/log.hpp"
#include "common/timing.hpp"

#if defined(BRIM_WITH_OPENCV)
#include <opencv2/core.hpp>
#include <opencv2/imgproc.hpp>
#include <opencv2/objdetect/face.hpp>
#endif

namespace brim::faces {

bool valid_name(const std::string& n) {
  if (n.empty() || n.size() > 32) return false;
  if (!std::isalpha(static_cast<unsigned char>(n[0]))) return false;
  for (char c : n)
    if (!std::isalpha(static_cast<unsigned char>(c)) && c != ' ' && c != '-' && c != '\'') return false;
  return true;
}

std::string named_text(const std::string& text, const std::string& name) {
  static const std::string kPerson = "Person ahead";
  if (!valid_name(name) || text.compare(0, kPerson.size(), kPerson) != 0) return text;
  return name + text.substr(6);  // keep " ahead, move left" / " ahead, stop"
}

// ------------------------------------------------------------------ gallery
namespace {
bool normalise(std::vector<float>& v) {
  double n = 0;
  for (float x : v) n += static_cast<double>(x) * x;
  if (!(n > 1e-12) || !std::isfinite(n)) return false;
  const float inv = static_cast<float>(1.0 / std::sqrt(n));
  for (float& x : v) x *= inv;
  return true;
}
}  // namespace

void Gallery::add(const std::string& name, std::vector<float> e) {
  if (!valid_name(name) || e.empty() || (dim_ && static_cast<int>(e.size()) != dim_) || !normalise(e)) return;
  dim_ = static_cast<int>(e.size());
  people_[name].push_back(std::move(e));
}

Gallery Gallery::load(const std::string& dir, const std::string& embedder_id) {
  Gallery g;
  DIR* d = opendir(dir.c_str());
  if (!d) return g;  // no folder = nobody enrolled
  int skipped = 0;
  while (dirent* ent = readdir(d)) {
    const std::string folder = ent->d_name;
    if (folder.empty() || folder[0] == '.') continue;
    std::string err;
    auto j = json::parse_file(dir + "/" + folder + "/embeddings.json", &err);
    if (!j || !j->is_object() || !valid_name(folder) || j->get_string("name") != folder ||
        j->get_string("model") != embedder_id) {
      ++skipped;
      continue;
    }
    const json::Value* arr = j->find("embeddings");
    if (!arr || !arr->is_array()) {
      ++skipped;
      continue;
    }
    for (const auto& row : arr->as_array()) {
      if (!row.is_array() || row.as_array().size() < 64 || row.as_array().size() > 1024) continue;
      std::vector<float> e;
      e.reserve(row.as_array().size());
      bool ok = true;
      for (const auto& x : row.as_array()) {
        if (!x.is_number()) ok = false;
        e.push_back(static_cast<float>(x.as_double()));
      }
      if (ok) g.add(folder, std::move(e));
    }
  }
  closedir(d);
  if (skipped) BRIM_LOGW("faces", "skipped %d folder(s) in the faces dir (bad name, other model or broken file)", skipped);
  return g;
}

Match Gallery::match(const std::vector<float>& raw, double threshold, double min_margin) const {
  Match m;
  if (people_.empty() || static_cast<int>(raw.size()) != dim_) return m;
  std::vector<float> e = raw;
  if (!normalise(e)) return m;
  double best = -2, second = -2;
  const std::string* best_name = nullptr;
  for (const auto& [name, embs] : people_) {
    double mine = -2;
    for (const auto& ref : embs) {
      double dot = 0;
      for (int i = 0; i < dim_; ++i) dot += static_cast<double>(ref[i]) * e[i];
      mine = std::max(mine, dot);
    }
    if (mine > best) {
      second = best;
      best = mine;
      best_name = &name;
    } else {
      second = std::max(second, mine);
    }
  }
  m.score = best;
  m.margin = people_.size() > 1 ? best - second : best;  // one person: nobody to confuse them with
  if (best_name && best >= threshold && m.margin >= min_margin) m.name = *best_name;
  return m;
}

// ------------------------------------------------------------------ names
void NameCache::set(int track_id, const std::string& name, std::int64_t now_ms) {
  if (track_id < 0 || !valid_name(name)) return;
  names_[track_id] = Entry{name, now_ms + ttl_ms_};
}

std::string NameCache::get(int track_id, std::int64_t now_ms) const {
  auto it = names_.find(track_id);
  if (it == names_.end() || now_ms > it->second.until_ms) return {};
  return it->second.name;
}

void NameCache::expire(std::int64_t now_ms) {
  for (auto it = names_.begin(); it != names_.end();) it = now_ms > it->second.until_ms ? names_.erase(it) : std::next(it);
}

void NameCache::keep_only(const Gallery& g) {
  for (auto it = names_.begin(); it != names_.end();) it = g.has(it->second.name) ? std::next(it) : names_.erase(it);
}

// ------------------------------------------------------------------ embedder
namespace {
class UnavailableEmbedder final : public FaceEmbedder {
 public:
  bool load() override { return false; }
  bool embed(const capture::Frame&, const detect::Box&, std::vector<float>&) override { return false; }
  const char* name() const override { return "built without OpenCV"; }
};

#if defined(BRIM_WITH_OPENCV)
// TODO(qnx-check): build against OpenCV 4.12 from oss.qnx.com (objdetect + dnn modules) and run
//   once on the Pi: `brim` logs "face check avg/max ms" every 20 checks. TODO(hw): Pi timing.
class OpenCvEmbedder final : public FaceEmbedder {
 public:
  OpenCvEmbedder(const config::FaceConfig& cfg, const std::string& models_dir)
      : cfg_(cfg), det_path_(models_dir + "/" + cfg.detector_model), rec_path_(models_dir + "/" + cfg.embedder_model) {}

  bool load() override {
    try {  // OpenCV reports errors as exceptions; none may cross this module boundary
      det_ = cv::FaceDetectorYN::create(det_path_, "", cv::Size(320, 320), static_cast<float>(cfg_.face_min_score), 0.3f, 20);
      rec_ = cv::FaceRecognizerSF::create(rec_path_, "");
    } catch (const std::exception& e) {
      BRIM_LOGE("faces", "face models did not load: %s", e.what());
      return false;
    }
    return det_ && rec_;
  }

  bool embed(const capture::Frame& f, const detect::Box& b, std::vector<float>& out) override {
    try {
      // The face is in the top part of a person box: look there only.
      const int x0 = std::clamp(static_cast<int>(b.x * f.width), 0, f.width - 1);
      const int y0 = std::clamp(static_cast<int>(b.y * f.height), 0, f.height - 1);
      const int x1 = std::clamp(static_cast<int>((b.x + b.w) * f.width), x0 + 1, f.width);
      const int y1 = std::clamp(static_cast<int>((b.y + b.h * 0.6f) * f.height), y0 + 1, f.height);
      if (x1 - x0 < cfg_.min_face_px || y1 - y0 < cfg_.min_face_px) return false;
      const cv::Mat rgb(f.height, f.width, CV_8UC3, const_cast<std::uint8_t*>(f.rgb.data()));
      cv::cvtColor(rgb(cv::Rect(x0, y0, x1 - x0, y1 - y0)), bgr_, cv::COLOR_RGB2BGR);  // models want BGR
      det_->setInputSize(bgr_.size());
      det_->detect(bgr_, faces_);
      int best = -1;
      for (int i = 0; i < faces_.rows; ++i) {
        const float w = faces_.at<float>(i, 2), score = faces_.at<float>(i, 14);
        if (w < cfg_.min_face_px || score < cfg_.face_min_score) continue;
        if (best < 0 || score > faces_.at<float>(best, 14)) best = i;
      }
      if (best < 0) return false;
      rec_->alignCrop(bgr_, faces_.row(best), aligned_);
      rec_->feature(aligned_, feature_);
      out.assign(feature_.ptr<float>(0), feature_.ptr<float>(0) + feature_.total());
      return !out.empty();
    } catch (const std::exception& e) {
      BRIM_LOGW("faces", "face check failed: %s", e.what());
      return false;
    }
  }

  const char* name() const override { return "opencv-yunet-sface"; }

 private:
  config::FaceConfig cfg_;
  std::string det_path_, rec_path_;
  cv::Ptr<cv::FaceDetectorYN> det_;
  cv::Ptr<cv::FaceRecognizerSF> rec_;
  cv::Mat bgr_, faces_, aligned_, feature_;  // reused between checks
};
#endif
}  // namespace

std::unique_ptr<FaceEmbedder> make_face_embedder(const config::FaceConfig& cfg, const std::string& models_dir) {
#if defined(BRIM_WITH_OPENCV)
  return std::make_unique<OpenCvEmbedder>(cfg, models_dir);
#else
  (void)cfg;
  (void)models_dir;
  return std::make_unique<UnavailableEmbedder>();
#endif
}

bool opencv_available() {
#if defined(BRIM_WITH_OPENCV)
  return true;
#else
  return false;
#endif
}

// ------------------------------------------------------------------ recogniser
Recogniser::Recogniser(config::FaceConfig cfg, std::unique_ptr<FaceEmbedder> embedder)
    : cfg_(std::move(cfg)), embedder_(std::move(embedder)), cache_(cfg_.name_ttl_ms) {}

Recogniser::~Recogniser() { stop(); }

void Recogniser::start() {
  reload(timing::mono_ms());
  // TODO(qnx-check): give this thread a low priority (pthread_setschedparam) below the pipeline.
  worker_ = std::thread(&Recogniser::run, this);
}

void Recogniser::stop() {
  stop_ = true;
  cv_.notify_all();
  if (worker_.joinable()) worker_.join();
}

void Recogniser::offer(const capture::Frame& frame, const detect::Detection& person, std::int64_t now_ms) {
  if (!opencv_available() || enrolled_ == 0 || embedder_failed_ || person.track_id < 0 || !person.is_person()) return;
  std::lock_guard<std::mutex> lk(mu_);
  if (busy_ || job_ready_ || stop_) return;
  auto& last = last_check_[person.track_id];
  if (last != 0 && now_ms - last < cfg_.check_interval_ms) return;
  last = now_ms;
  job_frame_.width = frame.width;
  job_frame_.height = frame.height;
  job_frame_.rgb.assign(frame.rgb.begin(), frame.rgb.end());  // capacity reused after the first copy
  job_person_ = person;
  job_now_ = now_ms;
  job_ready_ = true;
  cv_.notify_one();
}

std::string Recogniser::name_for(int track_id, std::int64_t now_ms) {
  if (enrolled_ == 0) return {};
  std::lock_guard<std::mutex> lk(mu_);
  return cache_.get(track_id, now_ms);
}

std::string Recogniser::status() const {
  if (!opencv_available() || embedder_failed_)
    return std::string("unavailable: ") + (embedder_ ? embedder_->name() : "no embedder") + ", nobody is named";
  const std::size_t n = enrolled_;
  if (n == 0) return "off (nobody enrolled)";
  return std::to_string(n) + " enrolled";
}

void Recogniser::reload(std::int64_t now_ms) {
  Gallery g = Gallery::load(cfg_.faces_dir, cfg_.embedder_id);  // outside the lock: file reads
  std::lock_guard<std::mutex> lk(mu_);
  const std::size_t before = gallery_.people();
  gallery_ = std::move(g);
  cache_.keep_only(gallery_);  // a removed person is never said again
  if (gallery_.empty()) {
    cache_.clear();
    last_check_.clear();
  }
  enrolled_ = gallery_.people();
  last_reload_ms_ = now_ms;
  if (gallery_.people() != before) BRIM_LOGI("faces", "%zu enrolled", gallery_.people());  // a count, never names
}

void Recogniser::run() {
  int runs = 0;
  double sum_ms = 0, max_ms = 0;
  std::vector<float> emb;
  emb.reserve(512);
  while (!stop_) {
    {
      std::unique_lock<std::mutex> lk(mu_);
      cv_.wait_for(lk, std::chrono::milliseconds(cfg_.gallery_reload_ms), [&] { return job_ready_ || stop_.load(); });
      if (stop_) break;
    }
    const std::int64_t now = timing::mono_ms();
    if (now - last_reload_ms_ >= cfg_.gallery_reload_ms) reload(now);
    {
      std::lock_guard<std::mutex> lk(mu_);
      cache_.expire(now);
      for (auto it = last_check_.begin(); it != last_check_.end();)
        it = now - it->second > 60000 ? last_check_.erase(it) : std::next(it);
      if (!job_ready_) continue;
      job_ready_ = false;
      busy_ = true;
    }
    // Nobody enrolled: no face model is loaded or run.
    if (enrolled_ > 0 && !embedder_loaded_ && !embedder_failed_) {
      embedder_loaded_ = embedder_->load();
      embedder_failed_ = !embedder_loaded_;
      if (embedder_failed_) BRIM_LOGW("faces", "face embedder %s: nobody will be named", embedder_->name());
    }
    std::string name;
    bool face = false;
    if (embedder_loaded_ && enrolled_ > 0) {
      const std::int64_t t0 = timing::mono_ns();
      face = embedder_->embed(job_frame_, job_person_.box, emb);
      const double ms = static_cast<double>(timing::mono_ns() - t0) / 1e6;
      last_ms_ = ms;
      sum_ms += ms, max_ms = std::max(max_ms, ms), ++runs;
      if (runs == 20) {
        BRIM_LOGI("faces", "face check avg %.1f / max %.1f ms over 20 checks (off the frame path)", sum_ms / runs, max_ms);
        runs = 0, sum_ms = 0, max_ms = 0;
      }
    }
    {
      std::lock_guard<std::mutex> lk(mu_);
      if (face) name = gallery_.match(emb, cfg_.match_threshold, cfg_.min_margin).name;
      // Unsure (no face, weak or ambiguous match): forget any name. Never keep a stale one.
      if (!name.empty()) cache_.set(job_person_.track_id, name, job_now_);
      else cache_.forget(job_person_.track_id);
      busy_ = false;
    }
    std::fill(emb.begin(), emb.end(), 0.0f);  // the embedding of whoever this was is dropped now
  }
}

}  // namespace brim::faces
