#include "common/config.hpp"

#include <cstdlib>

#include "common/log.hpp"

namespace brim::config {

std::string config_dir() {
  const char* env = std::getenv("BRIM_CONFIG_DIR");
  return env && *env ? env : "config";
}

CueThresholds load_cues(const std::string& path) {
  CueThresholds c;
  std::string err;
  auto j = json::parse_file(path, &err);
  if (!j || !j->is_object()) {
    BRIM_LOGW("config", "using default cue thresholds (%s)", err.c_str());
    return c;
  }
  c.recognize_min_confidence = j->get_number("recognize_min_confidence", c.recognize_min_confidence);
  c.unknown_max_confidence = j->get_number("unknown_max_confidence", c.unknown_max_confidence);
  c.name_repeat_s = j->get_int("name_repeat_s", c.name_repeat_s);
  c.cue_min_interval_s = j->get_int("cue_min_interval_s", c.cue_min_interval_s);
  c.cue_max_age_ms = j->get_int("cue_max_age_ms", c.cue_max_age_ms);
  c.stranger_forget_s = j->get_int("stranger_forget_s", c.stranger_forget_s);
  c.safety_head_height_m = j->get_number("safety_head_height_m", c.safety_head_height_m);
  c.safety_fast_approach_mps = j->get_number("safety_fast_approach_mps", c.safety_fast_approach_mps);
  // Privacy/spec floor: never allow configuration to make cues chattier than 1 per 10 s.
  if (c.cue_min_interval_s < 10) c.cue_min_interval_s = 10;
  if (c.stranger_forget_s > 1800) c.stranger_forget_s = 1800;
  return c;
}

AppConfig load_app(const std::string& path) {
  AppConfig a;
  std::string err;
  auto j = json::parse_file(path, &err);
  if (!j || !j->is_object()) {
    BRIM_LOGW("config", "using default app config (%s)", err.c_str());
    return a;
  }
  a.data_dir = j->get_string("data_dir", a.data_dir);
  a.run_dir = j->get_string("run_dir", a.run_dir);
  a.audio_dir = j->get_string("audio_dir", a.audio_dir);
  a.models_dir = j->get_string("models_dir", a.models_dir);
  a.http_port = static_cast<int>(j->get_int("http_port", a.http_port));
  a.capture_fps = static_cast<int>(j->get_int("capture_fps", a.capture_fps));
  a.health_period_ms = static_cast<int>(j->get_int("health_period_ms", a.health_period_ms));
  a.heartbeat_period_ms = static_cast<int>(j->get_int("heartbeat_period_ms", a.heartbeat_period_ms));
  return a;
}

}  // namespace brim::config
