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

std::string trim(const std::string& s) {
  auto b = s.find_first_not_of(" \t\r\n");
  if (b == std::string::npos) return "";
  auto e = s.find_last_not_of(" \t\r\n");
  return s.substr(b, e - b + 1);
}

// Counts UTF-8 code points (display names may be non-ASCII).
std::size_t utf8_len(const std::string& s) {
  std::size_t n = 0;
  for (unsigned char c : s)
    if ((c & 0xC0) != 0x80) ++n;
  return n;
}

}  // namespace

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
  if (p == "/api/people" && req.method == "GET") return json_response(200, to_array(db_.list_people()));
  if (p.rfind("/api/people/", 0) == 0) {
    std::string id_s = p.substr(12);
    char* end = nullptr;
    long long id = std::strtoll(id_s.c_str(), &end, 10);
    if (id_s.empty() || *end != '\0' || id <= 0) return error(400, "bad person id");
    return people_item(req, id);
  }
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

HttpResponse Api::people_item(const HttpRequest& req, std::int64_t id) {
  if (req.method == "PATCH") {
    auto j = json::parse(req.body);
    schema::PersonPatch patch;
    if (!j || !schema::from_json(*j, patch)) return error(400, "expected {\"display_name\": \"...\"}");
    std::string name = trim(patch.display_name);
    if (name.empty() || utf8_len(name) > 60) return error(400, "display_name must be 1-60 characters");
    auto p = db_.rename_person(id, name);
    if (!p) return error(404, "no such person");
    schema::Command c;
    c.t_mono_ms = timing::mono_ms();
    c.command = schema::CommandKind::ReloadPeople;
    c.person_id = id;
    send_(c);
    return json_response(200, schema::to_json(*p));
  }
  if (req.method == "DELETE") {
    std::string err;
    switch (db_.delete_person(id, data_dir_, &err)) {
      case Db::DeleteResult::NotFound: return error(404, "no such person");
      case Db::DeleteResult::FileError:
        BRIM_LOGE("api", "forget person %lld failed: %s", static_cast<long long>(id), err.c_str());
        return error(500, "could not delete this person's files; nothing was removed");
      case Db::DeleteResult::Deleted: break;
    }
    schema::Command c;
    c.t_mono_ms = timing::mono_ms();
    c.command = schema::CommandKind::ForgetPerson;
    c.person_id = id;
    send_(c);
    BRIM_LOGI("api", "forgot person %lld (fingerprint + voice clip deleted)", static_cast<long long>(id));
    HttpResponse r;
    r.status = 204;
    return r;
  }
  if (req.method == "GET") {
    auto p = db_.get_person(id);
    if (!p) return error(404, "no such person");
    return json_response(200, schema::to_json(*p));
  }
  return error(405, "method not allowed");
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
  if (s.cue_min_interval_s < 10 || s.cue_min_interval_s > 600) return error(400, "cue_min_interval_s must be 10-600");
  if (!db_.put_settings(s)) return error(500, "could not save settings");
  schema::Command c;
  c.t_mono_ms = timing::mono_ms();
  c.command = schema::CommandKind::ReloadSettings;
  send_(c);
  return json_response(200, schema::to_json(db_.get_settings()));
}

}  // namespace brim::app
