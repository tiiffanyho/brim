// Host-only test tool: what brim would say for a stream of detector outputs.
//
// scripts/laptop/brim_test.py live runs the detector in Python and pipes each frame's raw model
// rows here, so the laptop test speaks brim's own decisions (decode thresholds, close warning,
// navigation) and never a Python copy of them. Same order as brim_main's pipeline: decide the way
// out, then the close warnings (which say that way out), then the navigation call.
//
// stdin, one line per frame:  <t_ms> <n> then n x <ymin> <xmin> <ymax> <xmax> <coco_class> <score>
// stdout, one line per frame: {"t":123,"says":[{"prio":"safety","text":"Person ahead, move left"},...]}
// Thresholds come from config/safety.json and config/nav.json (BRIM_CONFIG_DIR, default ./config).
// Nothing is stored: boxes live for one frame.
#include <iostream>
#include <sstream>
#include <string>
#include <vector>

#include "capture/camera.hpp"
#include "common/config.hpp"
#include "detect/detector.hpp"
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
    dets.clear();
    detect::decode_detections(boxes.data(), classes.data(), scores.data(), n, static_cast<float>(safety_cfg.min_confidence),
                              static_cast<float>(safety_cfg.phone_min_confidence), dets);
    frame.t_capture_mono_ms = t;
    auto path = navigator.update(dets, t);
    std::string says;
    for (const auto& a : safety_mon->check(frame, dets, navigator.current()))
      says += (says.empty() ? "" : ",") + std::string("{\"prio\":\"safety\",\"text\":") + quoted(a.text) + "}";
    if (path) says += (says.empty() ? "" : ",") + std::string("{\"prio\":\"nav\",\"text\":") + quoted(path->text) + "}";
    std::cout << "{\"t\":" << t << ",\"says\":[" << says << "]}" << std::endl;
  }
  return 0;
}
