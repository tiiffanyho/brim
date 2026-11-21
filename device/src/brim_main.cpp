// brim: the real-time pipeline process.
//
//   capture -> detect (people + objects, anonymous tracks) -> safety -> navigation
//                                      |                                  |
//                                      +---> events (EventBus -> app_server)
//                                      +---> SpeakQueue -> audio (earpiece)
//
// Speech order is safety first, then navigation, then everything else (see audio/speak_queue.hpp).
// No identity anywhere: no face recognition, no embeddings, no names. People are boxes with a
// throw-away track id, and tracks are cleared within 30 minutes.
//
// Threads:
//   pipeline  : per-frame loop (camera paced, 15 fps). In --fake mode a scripted scenario injects
//               realistic events instead of running models.
//   speaker   : pops the SpeakQueue (priority + silence gate) and plays audio.
//   health    : 1 Hz HealthEvent.
//   heartbeat : pulses the supervisor every 200 ms *only while the pipeline loop is making
//               progress*, so a hung pipeline gets restarted.
//   commands  : receives app_server commands (reload_settings, speak_pairing_pin).
//
// Flags: --fake            scripted fake events, no hardware (laptop default via scripts/dev.sh)
//        --hang-after <s>  demo: freeze the pipeline after s seconds (supervisor must restart us)
//        SIGUSR1           demo: freeze the pipeline now
#include <atomic>
#include <csignal>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <mutex>
#include <random>
#include <thread>
#include <vector>

#include "audio/audio_out.hpp"
#include "audio/speak_queue.hpp"
#include "capture/camera.hpp"
#include "common/config.hpp"
#include "common/events.hpp"
#include "common/log.hpp"
#include "common/sysinfo.hpp"
#include "common/timing.hpp"
#include "detect/detector.hpp"
#include "ipc/bus.hpp"
#include "nav/nav.hpp"
#include "safety/safety_monitor.hpp"

using namespace brim;
namespace s = brim::schema;

namespace {

std::atomic<bool> g_stop{false};
std::atomic<bool> g_hang{false};

void on_signal(int sig) {
  if (sig == SIGUSR1) g_hang = true;
  else g_stop = true;
}

struct Stats {
  std::atomic<std::int64_t> last_tick_ms{0};   // pipeline progress marker for the heartbeat
  std::atomic<double> fps{0};
  std::atomic<double> latency_ms{0};           // rolling avg capture -> event
  std::atomic<std::int64_t> tracks{0};         // live anonymous tracks
  void add_latency(double ms) { latency_ms = latency_ms * 0.8 + ms * 0.2; }
};

// Shared context for the pipeline, the fake scenario and the command thread.
struct Ctx {
  config::AppConfig app;
  config::SafetyThresholds safety;
  config::NavThresholds nav;
  config::DetectConfig detect;
  bool fake = false;
  events::EventStamper stamper{"brim"};
  ipc::EventBus* bus = nullptr;
  audio::SpeakQueue* speak = nullptr;
  audio::AudioOut* audio = nullptr;
  Stats stats;
  std::atomic<bool> detect_healthy{true};  // written by the pipeline, read by the health thread

  template <class E>
  void emit(E& e, std::int64_t capture_ms) {
    std::string json = stamper.stamp(e);  // sets t_mono_ms
    if (capture_ms > 0) stats.add_latency(static_cast<double>(e.t_mono_ms - capture_ms));
    bus->publish(std::move(json));
  }
  void say(audio::Priority p, std::string text, std::int64_t created_ms) {
    speak->push(audio::SpeechItem{p, std::move(text), "", created_ms});
  }

  // The wearer's settings. Defaults until app_server sends reload_settings (it does at startup).
  s::Settings settings() const {
    std::lock_guard<std::mutex> lk(settings_mu_);
    return settings_;
  }
  void apply_settings(const s::Settings& v) {
    {
      std::lock_guard<std::mutex> lk(settings_mu_);
      settings_ = v;
    }
    audio::SpeakQueueConfig sq;
    sq.info_max_age_ms = safety.info_max_age_ms;
    sq.info_min_interval_ms = v.info_min_interval_s * 1000;
    speak->set_config(sq);
    if (audio) audio->apply_settings(v);
  }

 private:
  mutable std::mutex settings_mu_;
  s::Settings settings_ = config::default_settings();
};

// Spoken form of a PIN: "1 2 3 4 5 6", so the earpiece reads digits, not "123,456".
std::string spell_digits(const std::string& pin) {
  std::string out;
  for (char c : pin) {
    if (c < '0' || c > '9') continue;
    if (!out.empty()) out.push_back(' ');
    out.push_back(c);
  }
  return out;
}

// ------------------------------------------------------------------ fake scenario
// A 20-second loop that looks like a walk down a corridor: a person passes, something is close in
// front, a chair is in the way, a phone is pointed at the wearer, and once there is no safe
// direction at all. Every event type the app knows about shows up in the first few seconds, so the
// demo (and scripts/smoke_test.sh) sees all of them even across a restart.
class FakeScenario {
 public:
  explicit FakeScenario(Ctx& c) : c_(c), rng_(42) {}

  void tick(std::int64_t now_ms) {
    if (start_ms_ == 0) start_ms_ = now_ms;
    const std::int64_t t = (now_ms - start_ms_) / 1000 % 20;
    if (t == last_t_) return;
    last_t_ = t;
    const std::int64_t cap = now_ms - std::uniform_int_distribution<int>(25, 70)(rng_);
    switch (t) {
      case 1:
        person(s::Direction::Left, s::Proximity::Near, 0.09, cap);
        break;
      case 3:
        alert(s::SafetyKind::PersonClose, s::Severity::Critical, "Person close ahead", s::Direction::Ahead, cap);
        path(true, s::Direction::Right, "Clear path on your right, step right", {s::Direction::Ahead}, cap);
        break;
      case 6:
        obstacle(s::ObstacleKind::Chair, s::Direction::Ahead, s::Proximity::Close, 0.26, cap);
        break;
      case 7:
        alert(s::SafetyKind::ObjectClose, s::Severity::Warning, "Chair close ahead", s::Direction::Ahead, cap);
        path(true, s::Direction::Left, "Chair ahead, step left", {s::Direction::Ahead, s::Direction::Right}, cap);
        break;
      case 10:
        alert(s::SafetyKind::PhoneRaised, s::Severity::Warning, "A phone is pointed at you", s::Direction::Ahead, cap);
        break;
      case 12:
        path(false, s::Direction::Unknown, "Path unclear, stop", {s::Direction::Left, s::Direction::Ahead, s::Direction::Right},
             cap);
        break;
      case 14:
        person(s::Direction::Right, s::Proximity::Close, 0.21, cap);
        break;
      case 16:
        obstacle(s::ObstacleKind::Pole, s::Direction::Left, s::Proximity::Near, 0.08, cap);
        break;
      case 18:
        path(true, s::Direction::Ahead, "Clear path ahead", {}, cap);
        break;
      default:
        break;
    }
    c_.stats.tracks = (t % 7 == 0) ? 0 : 1 + t % 3;  // plausible live-track count for Device health
  }

 private:
  static const char* side(s::Direction d) {
    switch (d) {
      case s::Direction::Left: return "on your left";
      case s::Direction::Right: return "on your right";
      case s::Direction::Ahead: return "ahead";
      default: return "";
    }
  }

  void person(s::Direction d, s::Proximity p, double area, std::int64_t cap) {
    s::PersonSeenEvent e;
    e.track_id = ++track_;
    e.direction = d;
    e.proximity = p;
    e.confidence = std::uniform_real_distribution<double>(0.82, 0.98)(rng_);
    e.area_frac = area;
    e.t_capture_mono_ms = cap;
    c_.emit(e, cap);
    if (c_.settings().announce_people)
      c_.say(audio::Priority::Info,
             std::string("Person ") + side(d) + (p == s::Proximity::Close ? ", close by" : ""), cap);
  }

  void obstacle(s::ObstacleKind k, s::Direction d, s::Proximity p, double area, std::int64_t cap) {
    s::ObstacleSeenEvent e;
    e.track_id = ++track_;
    e.label = k;
    e.direction = d;
    e.proximity = p;
    e.confidence = std::uniform_real_distribution<double>(0.70, 0.95)(rng_);
    e.area_frac = area;
    e.t_capture_mono_ms = cap;
    c_.emit(e, cap);
    if (c_.settings().announce_obstacles)
      c_.say(audio::Priority::Info, std::string(s::to_string(k)) + " " + side(d), cap);
  }

  void alert(s::SafetyKind k, s::Severity sev, const char* text, s::Direction d, std::int64_t cap) {
    s::SafetyAlertEvent e;
    e.kind = k;
    e.severity = sev;
    e.text = text;
    e.direction = d;
    e.proximity = s::Proximity::Close;
    e.confidence = std::uniform_real_distribution<double>(0.80, 0.99)(rng_);
    e.t_capture_mono_ms = cap;
    c_.emit(e, cap);
    c_.say(audio::Priority::Safety, text, cap);
  }

  void path(bool clear, s::Direction d, const char* text, std::vector<s::Direction> blocked, std::int64_t cap) {
    s::ClearPathEvent e;
    e.clear = clear;
    e.direction = d;
    e.text = text;
    if (!blocked.empty()) e.blocked = std::move(blocked);
    e.t_capture_mono_ms = cap;
    c_.emit(e, cap);
    c_.say(audio::Priority::Nav, text, cap);
  }

  Ctx& c_;
  std::mt19937 rng_;
  std::int64_t start_ms_ = 0;
  std::int64_t last_t_ = -1;
  std::int64_t track_ = 0;
};

// Fake VAD: "someone is talking" for 3 s out of every 15 s, to exercise the info silence gate.
class FakeVad final : public audio::VoiceActivity {
 public:
  bool someone_talking() override { return (timing::mono_ms() / 1000) % 15 < 3; }
};

// ------------------------------------------------------------------ threads
void pipeline_loop(Ctx& c, capture::Camera& cam, detect::Detector& det, safety::SafetyMonitor& safety_mon,
                   std::int64_t hang_after_ms) {
  FakeScenario fake(c);
  nav::Navigator navigator(c.nav, c.safety.min_confidence);
  detect::TrackStore tracks(c.safety.track_forget_s * 1000, c.safety.repeat_suppress_s * 1000,
                            static_cast<float>(c.detect.track_match_iou), c.detect.track_match_window_ms,
                            static_cast<float>(c.detect.track_center_gate));
  detect::DetectPacer pacer(c.detect.budget_ms, c.detect.stride);
  detect::StageStats stage_stats;
  std::int64_t stats_window_start = timing::mono_ms();
  std::int64_t last_dets_log_ms = 0;
  std::vector<detect::Detection> dets;  // reused every frame: no per-frame allocation
  dets.reserve(32);
  capture::Frame frame;
  std::int64_t start = timing::mono_ms();
  std::int64_t fps_window_start = start;
  int fps_frames = 0;
  bool detect_was_healthy = true;

  while (!g_stop) {
    if (hang_after_ms > 0 && timing::mono_ms() - start > hang_after_ms) g_hang = true;
    if (g_hang) {  // simulated deadlock: stop ticking, heartbeat stops, supervisor restarts us
      timing::sleep_ms(100);
      continue;
    }
    if (!cam.grab(frame)) {
      BRIM_LOGE("capture", "camera grab failed");
      timing::sleep_ms(50);
      continue;
    }
    timing::StageTimer frame_budget("frame", 1000 / (c.app.capture_fps > 0 ? c.app.capture_fps : 15));
    const std::int64_t now = timing::mono_ms();
    c.stats.last_tick_ms = now;
    ++fps_frames;
    if (now - fps_window_start >= 1000) {
      c.stats.fps = fps_frames * 1000.0 / static_cast<double>(now - fps_window_start);
      fps_frames = 0;
      fps_window_start = now;
    }

    if (c.fake) {
      fake.tick(now);
      continue;
    }

    // Real path: TFLite detector on the Pi (safety and nav models are still TODO(ai)).
    // Cannot-fail rule: a sick detector is spoken out loud, not hidden. We never infer "clear".
    c.detect_healthy = det.healthy();
    if (!det.healthy() || !safety_mon.healthy()) {
      if (detect_was_healthy) {
        detect_was_healthy = false;
        s::FaultEvent f;
        f.component = det.healthy() ? "safety" : "detect";
        f.action = s::FaultAction::Degraded;
        f.message = "cannot see: obstacle warnings are not reliable";
        c.emit(f, 0);
        c.say(audio::Priority::Safety, "Brim cannot see. Stop and ask for help", now);
      }
    } else if (!detect_was_healthy) {  // recovered
      detect_was_healthy = true;
      s::FaultEvent f;
      f.component = "detect";
      f.action = s::FaultAction::Recovered;
      f.message = "detection is working again";
      c.emit(f, 0);
      c.say(audio::Priority::Nav, "Brim is working again", now);
    }

    // Over budget? Then the detector runs on every 2nd frame (DetectPacer). Skipped frames do no
    // detection-based work at all: we never re-announce or re-judge from a stale frame.
    if (!pacer.tick()) continue;

    // Detection: resize -> invoke -> decode (timed inside the detector), then tracking.
    det.detect(frame, dets);
    const std::int64_t t_track = timing::mono_ns();
    tracks.observe(dets, now);
    const double track_ms = static_cast<double>(timing::mono_ns() - t_track) / 1e6;
    const auto st = det.last_stage_times();
    stage_stats.add(st, track_ms);
    if (pacer.record(st.total() + track_ms))
      BRIM_LOGW("detect", "detect avg %.1f ms vs %.0f ms budget: now running %s", pacer.average_ms(),
                c.detect.budget_ms, pacer.stride() == 2 ? "every 2nd frame" : "every frame");
    BRIM_LOGD("detect", "frame %llu: %zu found, resize %.1f + invoke %.1f + decode %.1f + track %.1f = %.1f ms (budget %.0f)",
              static_cast<unsigned long long>(frame.index), dets.size(), st.resize_ms, st.invoke_ms, st.decode_ms, track_ms,
              st.total() + track_ms, c.detect.budget_ms);
    if (now - stats_window_start >= c.detect.stats_period_s * 1000LL && stage_stats.runs > 0) {
      const double n = stage_stats.runs;
      const double avg_total = (stage_stats.sum.total() + stage_stats.track_sum) / n;
      const double max_total = stage_stats.max.total() + stage_stats.track_max;  // upper bound
      BRIM_LOGI("detect",
                "timing over %d runs (avg/max ms): resize %.1f/%.1f, invoke %.1f/%.1f, decode %.2f/%.2f, track %.2f/%.2f, "
                "total %.1f/%.1f vs %.0f ms budget %s, %s",
                stage_stats.runs, stage_stats.sum.resize_ms / n, stage_stats.max.resize_ms, stage_stats.sum.invoke_ms / n,
                stage_stats.max.invoke_ms, stage_stats.sum.decode_ms / n, stage_stats.max.decode_ms, stage_stats.track_sum / n,
                stage_stats.track_max, avg_total, max_total, c.detect.budget_ms, avg_total <= c.detect.budget_ms ? "OK" : "OVER",
                pacer.stride() == 2 ? "every 2nd frame" : "every frame");
      stage_stats.reset();
      stats_window_start = now;
    }
    if (!dets.empty() && now - last_dets_log_ms >= 1000) {  // what we see, once a second (boxes only, no identity)
      last_dets_log_ms = now;
      char line[512];
      int len = 0;
      for (const auto& d : dets) {
        if (len >= static_cast<int>(sizeof(line)) - 64) break;
        len += std::snprintf(line + len, sizeof(line) - len, "%s %s#%d %.2f [x%.2f y%.2f w%.2f h%.2f]", len ? "," : "",
                             detect::to_string(d.thing), d.track_id, d.score, d.box.x, d.box.y, d.box.w, d.box.h);
      }
      BRIM_LOGI("detect", "%zu seen:%s", dets.size(), line);
    }
    const auto settings = c.settings();

    // 1. Safety first: anything close in front, or a phone pointed at the wearer.
    for (auto& alert : safety_mon.check(frame, dets)) {
      alert.t_capture_mono_ms = frame.t_capture_mono_ms;
      c.emit(alert, frame.t_capture_mono_ms);
      c.say(audio::Priority::Safety, alert.text, frame.t_capture_mono_ms);
    }

    // 2. Navigation: the safest direction to move, or "Path unclear, stop". It runs after the
    //    safety alerts above, so a direction is never queued before the warning that made it
    //    necessary, and it only speaks once a call is confirmed over a few frames.
    if (auto path = navigator.update(dets, now)) {
      path->t_capture_mono_ms = frame.t_capture_mono_ms;
      const std::string text = path->text;
      c.emit(*path, frame.t_capture_mono_ms);
      c.say(audio::Priority::Nav, text, frame.t_capture_mono_ms);
      BRIM_LOGI("nav", "%s", text.c_str());
    }

    // 3. Everything else, lowest priority, rate-limited per track.
    for (const auto& d : dets) {
      if (d.score < c.safety.min_confidence) continue;  // never name what we did not clearly see
      const auto dir = detect::direction_of(d.box, c.nav.side_margin_frac);
      const auto prox = detect::proximity_of(d.box.area(),
                                             d.is_person() ? c.safety.person_close_area_frac : c.safety.object_close_area_frac,
                                             c.safety.near_area_frac);
      if (prox == s::Proximity::Far) continue;
      if (!tracks.should_announce(d.track_id, now)) continue;
      if (d.is_person()) {
        if (!settings.announce_people) continue;
        s::PersonSeenEvent e;
        e.track_id = d.track_id;
        e.direction = dir;
        e.proximity = prox;
        e.confidence = d.score;
        e.area_frac = d.box.area();
        e.t_capture_mono_ms = frame.t_capture_mono_ms;
        c.emit(e, frame.t_capture_mono_ms);
      } else {
        auto kind = detect::obstacle_kind_of(d.thing);
        if (!kind || !settings.announce_obstacles) continue;
        s::ObstacleSeenEvent e;
        e.track_id = d.track_id;
        e.label = *kind;
        e.direction = dir;
        e.proximity = prox;
        e.confidence = d.score;
        e.area_frac = d.box.area();
        e.t_capture_mono_ms = frame.t_capture_mono_ms;
        c.emit(e, frame.t_capture_mono_ms);
      }
    }
    tracks.expire(now);
    c.stats.tracks = static_cast<std::int64_t>(tracks.active(now));
  }
}

void speaker_loop(Ctx& c, audio::AudioOut& out, audio::VoiceActivity& vad) {
  while (!g_stop) {
    auto item = c.speak->pop_next(timing::mono_ms(), vad.someone_talking());
    if (!item) {
      timing::sleep_ms(50);
      continue;
    }
    out.play(*item);
  }
}

void health_loop(Ctx& c, const char* camera_name, const char* detector_name) {
  sysinfo::CpuMeter cpu;
  const std::int64_t start = timing::mono_ms();
  timing::PeriodicTimer timer(c.app.health_period_ms);
  while (!g_stop) {
    timer.wait();
    s::HealthEvent h;
    h.uptime_s = (timing::mono_ms() - start) / 1000.0;
    h.fps = c.stats.fps;
    h.cpu_pct = cpu.sample();
    h.temp_c = sysinfo::soc_temp_c();
    h.pipeline_latency_ms = c.stats.latency_ms;
    h.speak_queue_len = static_cast<std::int64_t>(c.speak->size());
    auto comp = [](const char* n, s::ComponentState st, std::string d = {}) {
      s::ComponentStatus cs{n, st, std::nullopt};
      if (!d.empty()) cs.detail = std::move(d);
      return cs;
    };
    bool pipeline_live = timing::mono_ms() - c.stats.last_tick_ms < 1000;
    h.components = {
        comp("pipeline", pipeline_live ? s::ComponentState::Ok : s::ComponentState::Down, c.fake ? "fake mode" : ""),
        comp("capture", c.fake ? s::ComponentState::Stub : s::ComponentState::Ok, camera_name),
        comp("detect",
             c.fake || !std::strcmp(detector_name, "stub") ? s::ComponentState::Stub
             : c.detect_healthy.load()                    ? s::ComponentState::Ok
                                                          : s::ComponentState::Down,
             std::string(detector_name) + ", " + std::to_string(c.stats.tracks.load()) + " tracks"),
        comp("safety", s::ComponentState::Stub),
        comp("audio", s::ComponentState::Stub, "log output"),
        comp("ipc", s::ComponentState::Ok, std::string(ipc::backend_name()) + ", dropped " + std::to_string(c.bus->dropped())),
    };
    c.emit(h, 0);
  }
}

void heartbeat_loop(Ctx& c, ipc::EventBus& sup) {
  while (!g_stop) {
    timing::sleep_ms(c.app.heartbeat_period_ms);
    // Only vouch for ourselves if the pipeline ticked recently.
    if (timing::mono_ms() - c.stats.last_tick_ms < 1000) sup.pulse(events::kPulseHeartbeat, 0);
  }
}

void command_loop(Ctx& c) {
  std::string err;
  auto ep = ipc::listen(events::kBrimCommandChannel, &err);
  if (!ep) {
    BRIM_LOGE("cmd", "cannot listen on %s: %s (commands disabled)", events::kBrimCommandChannel, err.c_str());
    return;
  }
  while (!g_stop) {
    auto m = ep->receive(200);
    if (!m || m->kind != ipc::Message::Kind::Event) continue;
    auto j = json::parse(m->payload);
    s::Command cmd;
    if (!j || !s::from_json(*j, cmd, &err)) {
      BRIM_LOGW("cmd", "bad command: %s", err.c_str());
      continue;
    }
    switch (cmd.command) {
      case s::CommandKind::ReloadSettings:
        if (!cmd.settings) {
          BRIM_LOGW("cmd", "reload_settings without settings payload, ignored");
          break;
        }
        c.apply_settings(*cmd.settings);
        BRIM_LOGI("cmd", "reload_settings applied (volume=%lld, rate=%.2f, people=%s, obstacles=%s, info_min_interval=%llds)",
                  static_cast<long long>(cmd.settings->volume), cmd.settings->speech_rate,
                  cmd.settings->announce_people ? "on" : "off", cmd.settings->announce_obstacles ? "on" : "off",
                  static_cast<long long>(cmd.settings->info_min_interval_s));
        break;
      case s::CommandKind::SpeakPairingPin: {
        const std::string digits = cmd.pin ? spell_digits(*cmd.pin) : "";
        if (digits.empty()) {
          BRIM_LOGW("cmd", "speak_pairing_pin without a PIN, ignored");
          break;
        }
        // Nav priority: the wearer is waiting to hear it, but a real warning still comes first.
        c.say(audio::Priority::Nav, "Pairing code " + digits, timing::mono_ms());
        BRIM_LOGI("cmd", "speaking pairing code in the earpiece");
        break;
      }
    }
  }
}

}  // namespace

int main(int argc, char** argv) {
  log::set_process_name("brim");
  bool fake = false;
  std::int64_t hang_after_ms = 0;
  for (int i = 1; i < argc; ++i) {
    if (!std::strcmp(argv[i], "--fake")) fake = true;
    else if (!std::strcmp(argv[i], "--hang-after") && i + 1 < argc) hang_after_ms = std::atoll(argv[++i]) * 1000;
    else if (!std::strcmp(argv[i], "--debug")) log::set_level(log::Level::Debug);
    else {
      std::fprintf(stderr, "usage: brim [--fake] [--hang-after SECONDS] [--debug]\n");
      return 2;
    }
  }
  std::signal(SIGINT, on_signal);
  std::signal(SIGTERM, on_signal);
  std::signal(SIGUSR1, on_signal);
  std::signal(SIGPIPE, SIG_IGN);

  Ctx c;
  c.fake = fake;
  c.app = config::load_app(config::config_dir() + "/app.json");
  c.safety = config::load_safety(config::config_dir() + "/safety.json");
  c.nav = config::load_nav(config::config_dir() + "/nav.json");
  c.detect = config::load_detect(config::config_dir() + "/detect.json");

  ipc::EventBus bus(events::kAppServerChannel);
  ipc::EventBus supervisor_bus(events::kSupervisorChannel, 4);
  audio::SpeakQueue speak({c.safety.info_max_age_ms, c.safety.info_min_interval_s * 1000, 32});
  c.bus = &bus;
  c.speak = &speak;

  // Module selection: fake/stub on the laptop, real (still stubbed inside) on QNX.
#if defined(__QNX__)
  const bool use_hw = !fake;
#else
  const bool use_hw = false;
#endif
  auto cam = use_hw ? capture::make_qnx_camera(c.app.capture_fps) : capture::make_fake_camera(c.app.capture_fps);
  detect::DetectorConfig dcfg;
  dcfg.model_path = c.app.models_dir + "/" + c.detect.model_file;
  dcfg.threads = c.detect.threads;
  dcfg.min_confidence = static_cast<float>(c.safety.min_confidence);
  dcfg.phone_min_confidence = static_cast<float>(c.safety.phone_min_confidence);
  auto det = use_hw ? detect::make_tflite_detector(dcfg) : detect::make_stub_detector();
  auto safety_mon = use_hw ? safety::make_vision_safety_monitor(c.safety, c.nav) : safety::make_stub_safety_monitor();
  auto audio_out = use_hw ? audio::make_qnx_audio_out(c.app.audio_dir) : audio::make_log_audio_out();
  std::unique_ptr<audio::VoiceActivity> vad = fake ? std::unique_ptr<audio::VoiceActivity>(new FakeVad) : audio::make_stub_vad();
  c.audio = audio_out.get();
  c.apply_settings(config::default_settings());  // until app_server sends the stored ones

  if (!cam->open()) {
    BRIM_LOGE("brim", "camera init failed");  // supervisor will restart us
    return 1;
  }
  // A detector that will not load is not a reason to die in a restart loop: keep running so the
  // pipeline sees det->healthy() == false and speaks "Brim cannot see" (cannot-fail rule).
  if (!det->load()) BRIM_LOGE("brim", "detector %s failed to load: running degraded, fault will be spoken", det->name());

  BRIM_LOGI("brim", "started (mode=%s, ipc=%s, camera=%s, detect=%s)", fake ? "fake" : "real", ipc::backend_name(), cam->name(),
            det->name());
  c.stats.last_tick_ms = timing::mono_ms();

  // TODO(qnx-check): pin the pipeline thread to its own core and raise its priority
  //   (pthread_setschedparam SCHED_RR; ThreadCtl(_NTO_TCTL_RUNMASK) for affinity on QNX).
  std::thread t_pipe(pipeline_loop, std::ref(c), std::ref(*cam), std::ref(*det), std::ref(*safety_mon), hang_after_ms);
  std::thread t_speak(speaker_loop, std::ref(c), std::ref(*audio_out), std::ref(*vad));
  std::thread t_health(health_loop, std::ref(c), cam->name(), det->name());
  std::thread t_hb(heartbeat_loop, std::ref(c), std::ref(supervisor_bus));
  std::thread t_cmd(command_loop, std::ref(c));

  while (!g_stop) timing::sleep_ms(100);
  BRIM_LOGI("brim", "stopping");
  t_pipe.join();
  t_speak.join();
  t_health.join();
  t_hb.join();
  t_cmd.join();
  return 0;
}
