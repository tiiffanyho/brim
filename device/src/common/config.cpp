#include "common/config.hpp"

#include <cstdlib>

#include "common/log.hpp"

namespace brim::config {

std::string config_dir() {
  const char* env = std::getenv("BRIM_CONFIG_DIR");
  return env && *env ? env : "config";
}

SafetyThresholds load_safety(const std::string& path) {
  SafetyThresholds c;
  std::string err;
  auto j = json::parse_file(path, &err);
  if (!j || !j->is_object()) {
    BRIM_LOGW("config", "using default safety thresholds (%s)", err.c_str());
    return c;
  }
  c.person_close_area_frac = j->get_number("person_close_area_frac", c.person_close_area_frac);
  c.object_close_area_frac = j->get_number("object_close_area_frac", c.object_close_area_frac);
  c.near_area_frac = j->get_number("near_area_frac", c.near_area_frac);
  c.min_confidence = j->get_number("min_confidence", c.min_confidence);
  c.phone_min_confidence = j->get_number("phone_min_confidence", c.phone_min_confidence);
  c.repeat_suppress_s = j->get_int("repeat_suppress_s", c.repeat_suppress_s);
  c.track_forget_s = j->get_int("track_forget_s", c.track_forget_s);
  c.info_min_interval_s = j->get_int("info_min_interval_s", c.info_min_interval_s);
  c.info_max_age_ms = j->get_int("info_max_age_ms", c.info_max_age_ms);
  // Spec floors: never let configuration make the hat chattier than one low-priority announcement
  // per 10 s, and never let it keep track data longer than 30 minutes.
  if (c.info_min_interval_s < 10) c.info_min_interval_s = 10;
  if (c.track_forget_s > 1800) c.track_forget_s = 1800;
  return c;
}

NavThresholds load_nav(const std::string& path) {
  NavThresholds c;
  std::string err;
  auto j = json::parse_file(path, &err);
  if (!j || !j->is_object()) {
    BRIM_LOGW("config", "using default navigation thresholds (%s)", err.c_str());
    return c;
  }
  c.side_margin_frac = j->get_number("side_margin_frac", c.side_margin_frac);
  c.clear_min_width_frac = j->get_number("clear_min_width_frac", c.clear_min_width_frac);
  c.clear_max_area_frac = j->get_number("clear_max_area_frac", c.clear_max_area_frac);
  c.decision_hold_ms = j->get_int("decision_hold_ms", c.decision_hold_ms);
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

schema::Settings default_settings() {
  schema::Settings s;
  s.volume = 80;
  s.speech_rate = 1.0;
  s.announce_people = true;
  s.announce_obstacles = true;
  s.safety_sensitivity = schema::Sensitivity::Normal;
  s.info_min_interval_s = 10;
  return s;
}

}  // namespace brim::config
