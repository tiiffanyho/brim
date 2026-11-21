#include "app_server/relay.hpp"

#include "common/events.hpp"
#include "common/log.hpp"
#include "common/timing.hpp"
#include "ipc/bus.hpp"

namespace brim::app {

namespace s = brim::schema;

namespace {
template <class E>
bool valid(const json::Value& j, E& out, std::string& err) {
  return s::from_json(j, out, &err);
}
}  // namespace

std::string process_event(const std::string& payload, Db& db, ServerState& state) {
  auto j = json::parse(payload);
  std::string err = "not JSON";
  auto type = j ? s::brimEvent_type_of(*j) : std::nullopt;
  if (!type) {
    state.rejected++;
    BRIM_LOGW("relay", "dropping event with unknown type: %.120s", payload.c_str());
    return {};
  }
  const std::int64_t now = timing::mono_ms();
  bool ok = false;
  switch (*type) {
    case s::BrimEventType::PersonRecognized: {
      s::PersonRecognizedEvent e;
      if ((ok = valid(*j, e, err))) db.record_recognized(e.person_id, s::to_string(e.direction), e.confidence, e.t_mono_ms);
      break;
    }
    case s::BrimEventType::UnknownPerson: {
      s::UnknownPersonEvent e;
      if ((ok = valid(*j, e, err))) db.record_stranger(e.anon_id, s::to_string(e.direction), e.t_mono_ms);
      break;
    }
    case s::BrimEventType::Cue: {
      s::CueEvent e;
      ok = valid(*j, e, err);
      break;
    }
    case s::BrimEventType::SafetyAlert: {
      s::SafetyAlertEvent e;
      if ((ok = valid(*j, e, err))) db.add_alert(e);
      break;
    }
    case s::BrimEventType::Fault: {
      s::FaultEvent e;
      if ((ok = valid(*j, e, err)) && e.restart_count) state.restart_count = *e.restart_count;
      break;
    }
    case s::BrimEventType::Health: {
      s::HealthEvent e;
      if ((ok = valid(*j, e, err))) state.set_health(e);
      break;
    }
  }
  if (!ok) {
    state.rejected++;
    BRIM_LOGW("relay", "dropping invalid event: %s", err.c_str());
    return {};
  }
  const std::int64_t t_mono = j->get_int("t_mono_ms");
  if (j->get_string("source") != "supervisor") state.last_brim_event_mono_ms = now;
  // Same machine, same CLOCK_MONOTONIC: this is the true brim->app_server delay.
  state.add_relay_latency(static_cast<double>(now - t_mono));
  j->set("t_relay_mono_ms", json::Value(now));
  state.relayed++;
  return j->dump();
}

void run_relay(Db& db, ServerState& state, const Broadcast& broadcast, const std::atomic<bool>& stop) {
  std::string err;
  auto ep = ipc::listen(events::kAppServerChannel, &err);
  while (!ep && !stop) {  // keep serving REST; retry the bus every second
    BRIM_LOGE("relay", "cannot listen on %s: %s (retrying)", events::kAppServerChannel, err.c_str());
    timing::sleep_ms(1000);
    ep = ipc::listen(events::kAppServerChannel, &err);
  }
  if (!ep) return;
  BRIM_LOGI("relay", "listening for events on %s (%s)", events::kAppServerChannel, ipc::backend_name());
  std::int64_t next_purge = 0;
  while (!stop) {
    auto now = timing::mono_ms();
    if (now >= next_purge) {
      int n = db.purge();
      if (n > 0) BRIM_LOGI("relay", "privacy purge removed %d old rows", n);
      next_purge = now + 60000;
    }
    auto m = ep->receive(250);
    if (!m || m->kind != ipc::Message::Kind::Event) continue;
    std::string out = process_event(m->payload, db, state);
    if (!out.empty()) broadcast(std::move(out));
  }
}

}  // namespace brim::app
