#include "app_server/api.hpp"

#include <cstdlib>

#include "common/json.hpp"
#include "common/log.hpp"
#include "common/timing.hpp"

namespace brim::app {

namespace {

HttpResponse json_response(int status, const json::Value& v) {
  HttpResponse r;
  r.status = status;
  r.body = v.dump();
  return r;
}

HttpResponse error(int status, const std::string& msg, std::int64_t retry_after_s = 0) {
  schema::ApiError e;
  e.error = msg;
  if (retry_after_s > 0) e.retry_after_s = retry_after_s;
  auto r = json_response(status, schema::to_json(e));
  if (retry_after_s > 0) r.headers.emplace_back("retry-after:", std::to_string(retry_after_s));
  return r;
}

template <class T>
json::Value to_array(const std::vector<T>& items) {
  json::Value a = json::Value::array();
  for (const auto& x : items) a.push_back(schema::to_json(x));
  return a;
}

}  // namespace

schema::Command reload_settings_command(const schema::Settings& s) {
  schema::Command c;
  c.t_mono_ms = timing::mono_ms();
  c.command = schema::CommandKind::ReloadSettings;
  c.settings = s;
  return c;
}

schema::Command speak_pairing_pin_command(const std::string& pin) {
  schema::Command c;
  c.t_mono_ms = timing::mono_ms();
  c.command = schema::CommandKind::SpeakPairingPin;
  c.pin = pin;
  return c;
}

int query_int(const std::string& query, const std::string& key, int def, int lo, int hi) {
  std::string k = key + "=";
  std::size_t i = 0;
  while (i <= query.size()) {
    std::size_t end = query.find('&', i);
    if (end == std::string::npos) end = query.size();
    if (query.compare(i, k.size(), k) == 0) {
      int v = std::atoi(query.substr(i + k.size(), end - i - k.size()).c_str());
      return v < lo ? lo : v > hi ? hi : v;
    }
    i = end + 1;
  }
  return def;
}

bool Api::authorized(const std::string& cookie_header) {
  return db_.check_session(session_from_cookie_header(cookie_header));
}

HttpResponse Api::handle(const HttpRequest& req) {
  const std::string& p = req.path;
  if (p == "/api/pair") {
    if (req.method != "POST") return error(405, "method not allowed");
    return pair(req);
  }
  if (p.rfind("/api/", 0) != 0) return error(404, "not found");
  if (!authorized(req.cookie_header)) return error(401, "not paired");

  if (p == "/api/health" && req.method == "GET") return health();
  if (p == "/api/activity" && req.method == "GET")
    return json_response(200, to_array(db_.activity(query_int(req.query, "limit", 50, 1, 500))));
  if (p == "/api/alerts" && req.method == "GET")
    return json_response(200, to_array(db_.alerts(query_int(req.query, "limit", 50, 1, 500))));
  if (p == "/api/settings") return settings(req);
  return error(404, "not found");
}

HttpResponse Api::pair(const HttpRequest& req) {
  auto j = json::parse(req.body);
  schema::PairRequest pr;
  if (!j || !schema::from_json(*j, pr)) return error(400, "expected {\"pin\": \"123456\"}");
  std::int64_t retry = 0;
  switch (pairing_.attempt(pr.pin, timing::mono_ms(), &retry)) {
    case Pairing::Result::Locked:
      BRIM_LOGW("api", "pairing locked after repeated wrong PINs");
      return error(429, "too many attempts", retry);
    case Pairing::Result::Wrong:
      return error(401, "wrong PIN");
    case Pairing::Result::Ok:
      break;
  }
  std::string token = random_token();
  db_.add_session(token);
  BRIM_LOGI("api", "new phone paired");
  auto r = json_response(200, schema::to_json(schema::PairResponse{true}));
  // No `Secure` flag: plain HTTP on the local Wi-Fi (no internet, no TLS certs on the hat).
  // TODO: optional self-signed TLS via lws if the venue Wi-Fi is shared/untrusted.
  r.headers.emplace_back("set-cookie:", std::string(kSessionCookie) + "=" + token +
                                            "; HttpOnly; SameSite=Strict; Path=/; Max-Age=2592000");
  return r;
}

HttpResponse Api::health() {
  schema::HealthResponse h;
  const auto now = timing::mono_ms();
  h.server_uptime_s = (now - state_.start_mono_ms) / 1000.0;
  const auto last = state_.last_brim_event_mono_ms.load();
  h.brim_connected = last > 0 && now - last < 3000;
  if (last > 0) h.last_event_age_ms = now - last;
  h.ws_clients = state_.ws_clients.load();
  h.db_ok = db_.ping();
  h.restart_count = state_.restart_count.load();
  h.relay_latency_ms = state_.relay_latency_ms();
  h.last_health = state_.health();
  return json_response(200, schema::to_json(h));
}

HttpResponse Api::settings(const HttpRequest& req) {
  if (req.method == "GET") return json_response(200, schema::to_json(db_.get_settings()));
  if (req.method != "PUT") return error(405, "method not allowed");
  auto j = json::parse(req.body);
  schema::Settings s;
  std::string err;
  if (!j || !schema::from_json(*j, s, &err)) return error(400, err.empty() ? "invalid JSON" : err);
  if (s.volume < 0 || s.volume > 100) return error(400, "volume must be 0-100");
  if (s.speech_rate < 0.5 || s.speech_rate > 2.0) return error(400, "speech_rate must be 0.5-2.0");
  if (s.info_min_interval_s < 10 || s.info_min_interval_s > 600) return error(400, "info_min_interval_s must be 10-600");
  if (!db_.put_settings(s)) return error(500, "could not save settings");
  // The settings travel with the command: brim applies them without touching SQLite.
  send_(reload_settings_command(db_.get_settings()));
  return json_response(200, schema::to_json(db_.get_settings()));
}

}  // namespace brim::app
