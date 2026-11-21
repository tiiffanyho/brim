// Host-only test tool: what brim would say for a stream of detector outputs.
//
// scripts/laptop/brim_test.py live runs the detector in Python and pipes each frame's raw model
// rows here, so the laptop test speaks brim's own decisions (decode thresholds, close warning,
// navigation) and never a Python copy of them. Same order as brim_main's pipeline: decide the way
// out, then the close warnings (which say that way out), then the navigation call.
//
// stdin, one line per frame:  <t_ms> <n> then n x <ymin> <xmin> <ymax> <xmax> <coco_class> <score>
//                             [<m> then m x <row index> <dim> <dim floats>]   (optional face embeddings)
// stdout, one line per frame: {"t":123,"says":[{"prio":"safety","text":"Person ahead, move left",
//                             "spoken":"Tiffany ahead, move left","named":true},...]}
// Face embeddings (from scripts/laptop/brim_test.py, only while someone is enrolled) are matched
// with brim's own faces::Gallery against config/faces.json + data/faces; a warning or direction
// about that person box is named with faces::named_text. Nothing is kept between frames.
// Thresholds come from config/safety.json and config/nav.json (BRIM_CONFIG_DIR, default ./config).
// Nothing is stored: boxes live for one frame.
#include <iostream>
#include <sstream>
#include <string>
#include <vector>

#include "capture/camera.hpp"
#include "common/config.hpp"
#include "detect/detector.hpp"
#include "detect/faces.hpp"
#include "nav/nav.hpp"
#include "safety/safety_monitor.hpp"

using namespace brim;

namespace {
std::string quoted(const std::string& s) {
  std::string out = "\"";
  for (char c : s) {
    if (c == '"' || c == '\\') out += '\\';
    out += c;
  }
  return out + "\"";
}
}  // namespace

int main() {
  const auto safety_cfg = config::load_safety(config::config_dir() + "/safety.json");
  const auto nav_cfg = config::load_nav(config::config_dir() + "/nav.json");
  nav::Navigator navigator(nav_cfg, safety_cfg.min_confidence);
  auto safety_mon = safety::make_vision_safety_monitor(safety_cfg, nav_cfg);
  const auto face_cfg = config::load_faces(config::config_dir() + "/faces.json");
  const auto gallery = faces::Gallery::load(face_cfg.faces_dir, face_cfg.embedder_id);
  std::vector<int> subjects;
  std::vector<std::string> names;
  std::vector<float> boxes, classes, scores;
  std::vector<detect::Detection> dets;
  capture::Frame frame;
  std::string line;
  while (std::getline(std::cin, line)) {
    std::istringstream in(line);
    long long t = 0;
    int n = 0;
    if (!(in >> t >> n) || n < 0 || n > 1000) {
      std::cout << "{\"error\":\"bad line\"}" << std::endl;
      continue;
    }
    boxes.assign(4 * n, 0.f), classes.assign(n, 0.f), scores.assign(n, 0.f);
    for (int i = 0; i < n; ++i) in >> boxes[4 * i] >> boxes[4 * i + 1] >> boxes[4 * i + 2] >> boxes[4 * i + 3] >> classes[i] >> scores[i];
    // Names per input row, from the optional embeddings (a sure match only; else none).
    names.assign(n, std::string());
    int m = 0;
    if (in >> m && m > 0 && m <= n) {
      for (int k = 0; k < m; ++k) {
        int row = -1, dim = 0;
        if (!(in >> row >> dim) || dim <= 0 || dim > 1024) break;
        std::vector<float> e(dim);
        for (auto& x : e) in >> x;
        if (row >= 0 && row < n) names[row] = gallery.match(e, face_cfg.match_threshold, face_cfg.min_margin).name;
      }
    }
    // Decode one row at a time so each detection remembers its input row (as its track id here).
    dets.clear();
    for (int i = 0; i < n; ++i) {
      const std::size_t before = dets.size();
      detect::decode_detections(&boxes[4 * i], &classes[i], &scores[i], 1, static_cast<float>(safety_cfg.min_confidence),
                                static_cast<float>(safety_cfg.phone_min_confidence), dets);
      if (dets.size() > before) dets.back().track_id = i;
    }
    frame.t_capture_mono_ms = t;
    auto path = navigator.update(dets, t);
    std::string says;
    auto item = [&](const char* prio, const std::string& text, int row, bool is_person) {
      const std::string name = is_person && row >= 0 && row < n ? names[row] : std::string();
      const std::string spoken = faces::named_text(text, name);
      says += (says.empty() ? "" : ",") + std::string("{\"prio\":\"") + prio + "\",\"text\":" + quoted(text) +
              ",\"spoken\":" + quoted(spoken) + ",\"named\":" + (spoken != text ? "true" : "false") + "}";
    };
    const auto alerts = safety_mon->check(frame, dets, navigator.current(), &subjects);
    for (std::size_t i = 0; i < alerts.size(); ++i)
      item("safety", alerts[i].text, i < subjects.size() ? subjects[i] : -1,
           alerts[i].kind == schema::SafetyKind::PersonClose);
    if (path) {
      const auto* cur = navigator.current();
      item("nav", path->text, cur ? cur->because_track : -1, cur && cur->because == detect::Thing::Person);
    }
    std::cout << "{\"t\":" << t << ",\"says\":[" << says << "]}" << std::endl;
  }
  return 0;
}
