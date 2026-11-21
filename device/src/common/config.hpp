// Loads config/safety.json, config/nav.json and config/app.json. Missing files or keys fall back to
// safe defaults so the device always boots (cannot-fail rule): a bad config is logged, never fatal.
#pragma once

#include <cstdint>
#include <string>

#include "common/json.hpp"
#include "events.gen.hpp"

namespace brim::config {

// Safety thresholds. The camera is the only sensor, so "how close" is estimated from how much of
// the frame a box fills (area fraction), never from a distance reading.
struct SafetyThresholds {
  double person_close_area_frac = 0.18;   // person box this big => "close in front" => safety alert
  double object_close_area_frac = 0.22;   // same for a non-person obstacle
  double near_area_frac = 0.06;           // below this a box is "far" and not worth saying
  double min_confidence = 0.60;           // never name an obstacle we did not see this confidently
  double phone_min_confidence = 0.55;     // phone-held-up-toward-you alert
  std::int64_t repeat_suppress_s = 10;    // don't repeat the same track/alert more often than this
  std::int64_t track_forget_s = 1800;     // privacy: track data cleared after 30 minutes
  std::int64_t info_min_interval_s = 10;  // at most one low-priority announcement per this long
  std::int64_t info_max_age_ms = 3000;    // low-priority speech older than this is dropped, not said late
};

// Navigation thresholds: how the frame is split and how sure we must be to name a direction.
struct NavThresholds {
  double side_margin_frac = 0.33;       // left third / middle / right third of the frame
  double clear_min_width_frac = 0.25;   // a gap narrower than this is not a path
  double clear_max_area_frac = 0.04;    // a direction with a bigger box in it is not clear
  std::int64_t decision_hold_ms = 1500; // keep a direction this long before changing our advice
  int confirm_frames = 3;               // matching frames in a row before a call is spoken (2 or 3)
};

// Detector and tracker (config/detect.json). Confidence thresholds stay in SafetyThresholds.
struct DetectConfig {
  std::string model_file = "efficientdet_lite0.tflite";  // inside AppConfig::models_dir
  int threads = 4;                         // TFLite CPU threads (Pi 5 has 4 cores)
  double budget_ms = 30;                   // per-frame detect budget (models/README.md)
  int stride = 0;                          // 0 = auto (every frame, every 2nd when over budget), 1 or 2 = fixed
  double track_match_iou = 0.3;            // overlap that means "same thing as last frame"
  std::int64_t track_match_window_ms = 1500;  // a track unseen this long is not matched again
  double track_center_gate = 0.6;          // centre jump (in box sizes) still counted as the same thing
  int stats_period_s = 5;                  // how often brim logs per-stage detect timings
};

struct AppConfig {
  std::string data_dir = "data";            // SQLite DB only (never frames, never audio of people)
  std::string run_dir = "/tmp/brim";        // UDS sockets on host
  std::string audio_dir = "audio";
  std::string models_dir = "models";
  int http_port = 8080;
  int capture_fps = 15;
  int health_period_ms = 1000;
  int heartbeat_period_ms = 200;
};

// Paths default to "config/safety.json" etc. relative to cwd; override the dir with BRIM_CONFIG_DIR.
SafetyThresholds load_safety(const std::string& path);
NavThresholds load_nav(const std::string& path);
AppConfig load_app(const std::string& path);
DetectConfig load_detect(const std::string& path);
std::string config_dir();

// The one definition of the wearer's default settings, shared by brim (before app_server has sent
// any) and by app_server's SQLite layer (Db::default_settings).
schema::Settings default_settings();

}  // namespace brim::config
