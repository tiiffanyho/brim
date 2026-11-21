// Loads config/cues.json and config/app.json. Missing files or keys fall back to safe defaults
// so the device always boots (cannot-fail rule): a bad config is logged, never fatal.
#pragma once

#include <cstdint>
#include <string>

#include "common/json.hpp"

namespace brim::config {

struct CueThresholds {
  double recognize_min_confidence = 0.80;   // below this a face is treated as unknown
  double unknown_max_confidence = 0.55;     // below this we are confident it's a stranger
  std::int64_t name_repeat_s = 120;         // don't repeat "Sam is here" more often than this
  std::int64_t cue_min_interval_s = 10;     // at most one cue per this many seconds
  std::int64_t cue_max_age_ms = 3000;       // cues older than this are dropped, never spoken late
  std::int64_t stranger_forget_s = 1800;    // anonymous strangers forgotten after 30 minutes
  double safety_head_height_m = 2.0;        // obstacle alert distance at head height
  double safety_fast_approach_mps = 2.5;
};

struct AppConfig {
  std::string data_dir = "data";            // SQLite DB, embeddings, name clips (never video)
  std::string run_dir = "/tmp/brim";        // UDS sockets on host
  std::string audio_dir = "audio";
  std::string models_dir = "models";
  int http_port = 8080;
  int capture_fps = 15;
  int health_period_ms = 1000;
  int heartbeat_period_ms = 200;
};

// Paths default to "config/cues.json" etc. relative to cwd; override with BRIM_CONFIG_DIR.
CueThresholds load_cues(const std::string& path);
AppConfig load_app(const std::string& path);
std::string config_dir();

}  // namespace brim::config
