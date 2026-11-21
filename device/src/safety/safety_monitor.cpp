#include "safety/safety_monitor.hpp"

#include <algorithm>

#include "common/log.hpp"

namespace brim::safety {

namespace s = brim::schema;
using detect::Detection;
using detect::Thing;

namespace {
class StubSafetyMonitor final : public SafetyMonitor {
 public:
  bool healthy() const override { return true; }
  std::vector<s::SafetyAlertEvent> check(const capture::Frame&, const std::vector<Detection>&,
                                         const nav::FrameDecision*, std::vector<int>* subjects) override {
    if (subjects) subjects->clear();
    return {};
  }
};

class VisionSafetyMonitor final : public SafetyMonitor {
 public:
  VisionSafetyMonitor(const config::SafetyThresholds& safety, const config::NavThresholds& nav)
      : safety_(safety),
        margin_(nav.side_margin_frac > 0 && nav.side_margin_frac < 0.5 ? nav.side_margin_frac : 0.33),
        confirm_(std::clamp(nav.confirm_frames, 2, 3)) {}

  bool healthy() const override { return true; }

  std::vector<s::SafetyAlertEvent> check(const capture::Frame& frame, const std::vector<Detection>& dets,
                                         const nav::FrameDecision* nav_now, std::vector<int>* subjects) override {
    std::vector<s::SafetyAlertEvent> out;
    if (subjects) subjects->clear();
    const std::int64_t now = frame.t_capture_mono_ms;
    // The closest (biggest) thing close in front, people and objects judged separately.
    const Detection* person = nullptr;
    const Detection* object = nullptr;
    for (const auto& d : dets) {
      if (d.thing == Thing::Phone || d.score < safety_.min_confidence) continue;  // phones are never obstacles
      if (!in_corridor(d)) continue;
      const bool is_person = d.thing == Thing::Person;
      const double need = is_person ? safety_.person_close_area_frac : safety_.object_close_area_frac;
      if (d.box.area() < need) continue;
      const Detection*& slot = is_person ? person : object;
      if (!slot || d.box.area() > slot->box.area()) slot = &d;
    }
    if (track(person_, person, now, s::SafetyKind::PersonClose, s::Severity::Critical, nav_now, out) && subjects)
      subjects->push_back(person->track_id);
    if (track(object_, object, now, s::SafetyKind::ObjectClose, s::Severity::Warning, nav_now, out) && subjects)
      subjects->push_back(object->track_id);
    return out;
  }

 private:
  struct Watch {
    int streak = 0;
    std::int64_t last_alert_ms = -1;
  };

  bool in_corridor(const Detection& d) const {
    const bool lower_half = d.box.y + d.box.h > 0.5f;
    return lower_half && d.box.x < 1.0 - margin_ && d.box.x + d.box.w > margin_;
  }

  // True when an alert was added.
  bool track(Watch& w, const Detection* d, std::int64_t now, s::SafetyKind kind, s::Severity sev,
             const nav::FrameDecision* nav_now, std::vector<s::SafetyAlertEvent>& out) {
    if (!d) {
      w.streak = 0;
      return false;
    }
    if (++w.streak < confirm_) return false;  // one frame is a blip, never a warning
    if (w.last_alert_ms >= 0 && now - w.last_alert_ms < safety_.repeat_suppress_s * 1000) return false;
    w.last_alert_ms = now;
    s::SafetyAlertEvent a;
    a.kind = kind;
    a.severity = sev;
    a.direction = s::Direction::Ahead;
    a.proximity = s::Proximity::Close;
    a.confidence = d->score;
    a.text = nav::warning_text(d->thing, nav_now);
    out.push_back(std::move(a));
    return true;
  }

  config::SafetyThresholds safety_;
  double margin_;
  int confirm_;
  Watch person_, object_;
};
}  // namespace

std::unique_ptr<SafetyMonitor> make_stub_safety_monitor() { return std::make_unique<StubSafetyMonitor>(); }

std::unique_ptr<SafetyMonitor> make_vision_safety_monitor(const config::SafetyThresholds& safety,
                                                          const config::NavThresholds& nav) {
  return std::make_unique<VisionSafetyMonitor>(safety, nav);
}

}  // namespace brim::safety
