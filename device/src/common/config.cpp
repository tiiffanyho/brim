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
  c.confirm_frames = static_cast<int>(j->get_int("confirm_frames", c.confirm_frames));
  if (c.confirm_frames < 2) c.confirm_frames = 2;  // one frame is a blip, never a call
  if (c.confirm_frames > 3) c.confirm_frames = 3;
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
  a.models_dir = j->get_string("models_dir", a.models_dir);
  a.http_port = static_cast<int>(j->get_int("http_port", a.http_port));
  a.capture_fps = static_cast<int>(j->get_int("capture_fps", a.capture_fps));
  a.health_period_ms = static_cast<int>(j->get_int("health_period_ms", a.health_period_ms));
  a.heartbeat_period_ms = static_cast<int>(j->get_int("heartbeat_period_ms", a.heartbeat_period_ms));
  return a;
}

DetectConfig load_detect(const std::string& path) {
  DetectConfig c;
  std::string err;
  auto j = json::parse_file(path, &err);
  if (!j || !j->is_object()) {
    BRIM_LOGW("config", "using default detect config (%s)", err.c_str());
    return c;
  }
  c.model_file = j->get_string("model_file", c.model_file);
  c.threads = static_cast<int>(j->get_int("threads", c.threads));
  c.budget_ms = j->get_number("budget_ms", c.budget_ms);
  c.stride = static_cast<int>(j->get_int("stride", c.stride));
  c.track_match_iou = j->get_number("track_match_iou", c.track_match_iou);
  c.track_match_window_ms = j->get_int("track_match_window_ms", c.track_match_window_ms);
  c.track_center_gate = j->get_number("track_center_gate", c.track_center_gate);
  c.stats_period_s = static_cast<int>(j->get_int("stats_period_s", c.stats_period_s));
  if (c.threads < 1) c.threads = 1;
  if (c.threads > 4) c.threads = 4;
  if (c.stride < 0 || c.stride > 2) c.stride = 0;  // the spec allows every frame or every 2nd frame
  if (c.budget_ms <= 0) c.budget_ms = 30;
  if (c.stats_period_s < 1) c.stats_period_s = 5;
  return c;
}

namespace {
// [[lo, hi], ...] with 0 <= lo <= hi <= 179. Anything malformed keeps the defaults: a broken
// config must never make every pixel "red".
bool read_hue_ranges(const json::Value& j, std::string_view key, std::vector<HueRange>& out) {
  const json::Value* a = j.find(key);
  if (!a) return true;
  if (!a->is_array() || a->as_array().empty()) return false;
  std::vector<HueRange> v;
  for (const auto& r : a->as_array()) {
    if (!r.is_array() || r.as_array().size() != 2 || !r.as_array()[0].is_number() || !r.as_array()[1].is_number())
      return false;
    HueRange h{static_cast<int>(r.as_array()[0].as_int()), static_cast<int>(r.as_array()[1].as_int())};
    if (h.lo < 0 || h.hi > 179 || h.lo > h.hi) return false;
    v.push_back(h);
  }
  out = std::move(v);
  return true;
}
}  // namespace

TrafficConfig load_traffic(const std::string& path) {
  TrafficConfig c;
  std::string err;
  auto j = json::parse_file(path, &err);
  if (!j || !j->is_object()) {
    BRIM_LOGW("config", "using default traffic config, cue off (%s)", err.c_str());
    return c;
  }
  c.cue_enabled = j->get_bool("cue_enabled", c.cue_enabled);
  c.min_score = j->get_number("min_score", c.min_score);
  c.cue_interval_ms = j->get_int("cue_interval_s", c.cue_interval_ms / 1000) * 1000;
  c.min_bright = static_cast<int>(j->get_int("min_bright", c.min_bright));
  c.min_saturation = static_cast<int>(j->get_int("min_saturation", c.min_saturation));
  c.min_share = j->get_number("min_share", c.min_share);
  c.min_dominance = j->get_number("min_dominance", c.min_dominance);
  const TrafficConfig def;
  if (!read_hue_ranges(*j, "red_hue", c.red_hue)) {
    BRIM_LOGW("config", "traffic.json red_hue is malformed; using defaults");
    c.red_hue = def.red_hue;
  }
  if (!read_hue_ranges(*j, "green_hue", c.green_hue)) {
    BRIM_LOGW("config", "traffic.json green_hue is malformed; using defaults");
    c.green_hue = def.green_hue;
  }
  // Floors: one cue per light per 10 s at most, and a colour needs a real share of the box.
  if (c.cue_interval_ms < 10000) c.cue_interval_ms = 10000;
  if (c.min_score < 0.3 || c.min_score > 1) c.min_score = def.min_score;
  if (c.min_share <= 0 || c.min_share > 1) c.min_share = def.min_share;
  if (c.min_dominance < 1) c.min_dominance = def.min_dominance;
  if (c.min_bright < 0 || c.min_bright > 255) c.min_bright = def.min_bright;
  if (c.min_saturation < 0 || c.min_saturation > 255) c.min_saturation = def.min_saturation;
  return c;
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
