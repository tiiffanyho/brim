// brim: the real-time pipeline process.
//
//   capture -> faces -> recognize -> cues / safety -> events (EventBus -> app_server)
//                                                  \-> SpeakQueue -> audio
//
// Threads:
//   pipeline  : per-frame loop (camera paced, 15 fps). In --fake mode a scripted scenario injects
//               realistic events instead of running models.
//   speaker   : pops the SpeakQueue (priority + silence gate) and plays audio.
//   health    : 1 Hz HealthEvent.
//   heartbeat : pulses the supervisor every 200 ms *only while the pipeline loop is making
//               progress*, so a hung pipeline gets restarted.
//   commands  : receives app_server commands (forget_person, reload_settings).
//
// Flags: --fake            scripted fake events, no hardware (laptop default via scripts/dev.sh)
//        --hang-after <s>  demo: freeze the pipeline after s seconds (supervisor must restart us)
//        SIGUSR1           demo: freeze the pipeline now
#include <csignal>
#include <cstdlib>
#include <cstring>
#include <random>
#include <thread>

#include "audio/audio_out.hpp"
#include "audio/speak_queue.hpp"
#include "capture/camera.hpp"
#include "common/config.hpp"
#include "common/events.hpp"
#include "common/log.hpp"
#include "common/sysinfo.hpp"
#include "common/timing.hpp"
#include "cues/cue_engine.hpp"
#include "faces/face_detector.hpp"
#include "ipc/bus.hpp"
#include "recognize/recognizer.hpp"
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
  void add_latency(double ms) { latency_ms = latency_ms * 0.8 + ms * 0.2; }
};

// Shared context for the pipeline + fake scenario.
struct Ctx {
  config::AppConfig app;
  config::CueThresholds cues;
  bool fake = false;
  events::EventStamper stamper{"brim"};
  ipc::EventBus* bus = nullptr;
  audio::SpeakQueue* speak = nullptr;
  recognize::Gallery* gallery = nullptr;
  cues::NameAnnouncer* names = nullptr;
  Stats stats;

  template <class E>
  void emit(E& e, std::int64_t capture_ms) {
    std::string json = stamper.stamp(e);  // sets t_mono_ms
    if (capture_ms > 0) stats.add_latency(static_cast<double>(e.t_mono_ms - capture_ms));
    bus->publish(std::move(json));
  }
  void say(audio::Priority p, std::string text, std::int64_t created_ms) {
    speak->push(audio::SpeechItem{p, std::move(text), "", created_ms});
  }
};

// ------------------------------------------------------------------ fake scenario
// A 40-second loop that looks like a walk through a hallway. Names match the demo people seeded
// by `app_server --seed-demo` (ids 1..3).
struct FakePerson { std::int64_t id; const char* name; };
const FakePerson kSam{1, "Sam"}, kPriya{2, "Priya"}, kAlex{3, "Alex"};

class FakeScenario {
 public:
  explicit FakeScenario(Ctx& c) : c_(c), rng_(42) {}

  void tick(std::int64_t now_ms) {
    if (start_ms_ == 0) start_ms_ = now_ms;
    std::int64_t t = (now_ms - start_ms_) / 1000 % 40;
    if (t == last_t_) return;
    last_t_ = t;
    std::int64_t capture_ms = now_ms - std::uniform_int_distribution<int>(25, 70)(rng_);
    switch (t) {
      case 2: recognized(kSam, s::Direction::Left, capture_ms); break;
      case 5: cue(kSam, s::CueKind::Waving, "Sam is waving", capture_ms); break;
      case 9: stranger(s::Direction::Ahead, capture_ms); break;
      case 14: safety(s::SafetyKind::ObstacleHeadHeight, s::Severity::Critical, "Low branch ahead", 1.4, capture_ms); break;
      case 19: recognized(kPriya, s::Direction::Right, capture_ms); break;
      case 23: cue(kPriya, s::CueKind::Smiling, "Priya is smiling at you", capture_ms); break;
      case 29: safety(s::SafetyKind::StepOrCurb, s::Severity::Warning, "Step down ahead", 2.0, capture_ms); break;
      case 34: recognized(kAlex, s::Direction::Ahead, capture_ms); break;
      case 37: cue(kAlex, s::CueKind::Approaching, "Alex is coming toward you", capture_ms); break;
      default: break;
    }
  }

 private:
  void recognized(const FakePerson& p, s::Direction d, std::int64_t cap) {
    s::PersonRecognizedEvent e;
    e.person_id = p.id;
    e.display_name = p.name;
    e.confidence = std::uniform_real_distribution<double>(0.82, 0.98)(rng_);
    e.direction = d;
    e.t_capture_mono_ms = cap;
    c_.emit(e, cap);
    if (c_.names->should_announce(p.id, e.t_mono_ms)) c_.say(audio::Priority::Name, std::string(p.name) + " is here", cap);
  }
  void stranger(s::Direction d, std::int64_t cap) {
    s::UnknownPersonEvent e;
    e.anon_id = "stranger-" + std::to_string(++stranger_n_ % 5 + 1);
    e.direction = d;
    e.t_capture_mono_ms = cap;
    c_.emit(e, cap);
  }
  void cue(const FakePerson& p, s::CueKind k, const char* text, std::int64_t cap) {
    s::CueEvent e;
    e.cue = k;
    e.text = text;
    e.person_id = p.id;
    e.t_capture_mono_ms = cap;
    c_.emit(e, cap);
    c_.say(audio::Priority::Cue, text, cap);
  }
  void safety(s::SafetyKind k, s::Severity sev, const char* text, double dist, std::int64_t cap) {
    s::SafetyAlertEvent e;
    e.kind = k;
    e.severity = sev;
    e.text = text;
    e.distance_m = dist;
    e.direction = s::Direction::Ahead;
    e.t_capture_mono_ms = cap;
    c_.emit(e, cap);
    c_.say(audio::Priority::Safety, text, cap);
  }

  Ctx& c_;
  std::mt19937 rng_;
  std::int64_t start_ms_ = 0;
  std::int64_t last_t_ = -1;
  int stranger_n_ = 0;
};

// Fake VAD: "someone is talking" for 3 s out of every 15 s, to exercise the cue silence gate.
class FakeVad final : public audio::VoiceActivity {
 public:
  bool someone_talking() override { return (timing::mono_ms() / 1000) % 15 < 3; }
};

// ------------------------------------------------------------------ threads
void pipeline_loop(Ctx& c, capture::Camera& cam, faces::FaceDetector& det, recognize::Embedder& emb,
                   cues::CueEngine& cue_engine, safety::SafetyMonitor& safety_mon, std::int64_t hang_after_ms) {
  FakeScenario fake(c);
  recognize::StrangerTracker strangers(c.cues.stranger_forget_s * 1000);
  capture::Frame frame;
  std::int64_t start = timing::mono_ms();
  std::int64_t fps_window_start = start;
  int fps_frames = 0;

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

    // Real path (models are stubs today; see TODO(ai) in each module).
    for (auto& alert : safety_mon.check(frame)) {
      c.emit(alert, frame.t_capture_mono_ms);
      c.say(audio::Priority::Safety, alert.text, frame.t_capture_mono_ms);
    }
    std::vector<cues::TrackedPerson> people;
    for (const auto& box : det.detect(frame)) {
      auto e = emb.embed(frame, box);
      if (!e) continue;
      cues::TrackedPerson tp;
      tp.track_id = box.track_id;
      tp.box = box;
      if (auto m = c.gallery->best_match(*e, static_cast<float>(c.cues.recognize_min_confidence))) {
        s::PersonRecognizedEvent ev;
        ev.person_id = m->person_id;
        ev.display_name = m->display_name;
        ev.confidence = m->score;
        ev.direction = box.x + box.w / 2 < 0.33f ? s::Direction::Left : box.x + box.w / 2 > 0.66f ? s::Direction::Right : s::Direction::Ahead;
        ev.t_capture_mono_ms = frame.t_capture_mono_ms;
        if (c.names->should_announce(m->person_id, now)) {
          c.emit(ev, frame.t_capture_mono_ms);
          c.say(audio::Priority::Name, m->display_name + " is here", frame.t_capture_mono_ms);
        }
        tp.person_id = m->person_id;
        tp.display_name = m->display_name;
      } else {
        tp.anon_id = strangers.observe(*e, now);
      }
      people.push_back(std::move(tp));
    }
    for (auto& cue : cue_engine.update(frame, people)) {
      cue.t_capture_mono_ms = frame.t_capture_mono_ms;
      c.emit(cue, frame.t_capture_mono_ms);
      c.say(audio::Priority::Cue, cue.text, frame.t_capture_mono_ms);
    }
    strangers.expire(now);
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

void health_loop(Ctx& c, const char* camera_name, const char* faces_name) {
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
        comp("faces", s::ComponentState::Stub, faces_name),
        comp("recognize", s::ComponentState::Stub, std::to_string(c.gallery->size()) + " enrolled"),
        comp("cues", s::ComponentState::Stub),
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
      case s::CommandKind::ForgetPerson:
        if (cmd.person_id) {
          c.gallery->forget(*cmd.person_id);
          c.names->forget(*cmd.person_id);
          BRIM_LOGI("cmd", "forgot person %lld (embedding dropped from memory)", static_cast<long long>(*cmd.person_id));
        }
        break;
      case s::CommandKind::ReloadPeople:
        // TODO(ai): re-read names/embeddings for enrolled people (rename or new enrollment).
        BRIM_LOGI("cmd", "reload_people received (gallery reload not implemented yet)");
        break;
      case s::CommandKind::ReloadSettings:
        // TODO: read settings (volume, speech rate, cues on/off) — app_server should include them
        //   in the command payload or brim reads SQLite read-only. Apply to SpeakQueue/AudioOut.
        BRIM_LOGI("cmd", "reload_settings received (not applied yet)");
        break;
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
  c.cues = config::load_cues(config::config_dir() + "/cues.json");

  ipc::EventBus bus(events::kAppServerChannel);
  ipc::EventBus supervisor_bus(events::kSupervisorChannel, 4);
  audio::SpeakQueue speak({c.cues.cue_max_age_ms, c.cues.cue_min_interval_s * 1000, 32});
  recognize::Gallery gallery;
  cues::NameAnnouncer names(c.cues.name_repeat_s * 1000);
  c.bus = &bus;
  c.speak = &speak;
  c.gallery = &gallery;
  c.names = &names;

  // Module selection: fake/stub on the laptop, real (still stubbed inside) on QNX.
#if defined(__QNX__)
  const bool use_hw = !fake;
#else
  const bool use_hw = false;
#endif
  auto cam = use_hw ? capture::make_qnx_camera(c.app.capture_fps) : capture::make_fake_camera(c.app.capture_fps);
  auto det = use_hw ? faces::make_mediapipe_face_detector(c.app.models_dir) : faces::make_stub_face_detector();
  auto emb = use_hw ? recognize::make_tflite_embedder(c.app.models_dir) : recognize::make_stub_embedder();
  auto cue_engine = use_hw ? cues::make_mediapipe_cue_engine(c.cues) : cues::make_stub_cue_engine();
  auto safety_mon = use_hw ? safety::make_sensor_safety_monitor(c.cues) : safety::make_stub_safety_monitor();
  auto audio_out = use_hw ? audio::make_qnx_audio_out(c.app.audio_dir) : audio::make_log_audio_out();
  std::unique_ptr<audio::VoiceActivity> vad = fake ? std::unique_ptr<audio::VoiceActivity>(new FakeVad) : audio::make_stub_vad();

  if (!cam->open() || !det->load() || !emb->load()) {
    BRIM_LOGE("brim", "module init failed");  // supervisor will restart us
    return 1;
  }

  BRIM_LOGI("brim", "started (mode=%s, ipc=%s, camera=%s, faces=%s)", fake ? "fake" : "real", ipc::backend_name(), cam->name(), det->name());
  c.stats.last_tick_ms = timing::mono_ms();

  // TODO(qnx-check): pin the pipeline thread to its own core and raise its priority
  //   (pthread_setschedparam SCHED_RR; ThreadCtl(_NTO_TCTL_RUNMASK) for affinity on QNX).
  std::thread t_pipe(pipeline_loop, std::ref(c), std::ref(*cam), std::ref(*det), std::ref(*emb), std::ref(*cue_engine),
                     std::ref(*safety_mon), hang_after_ms);
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
