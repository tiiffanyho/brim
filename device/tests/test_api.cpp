// REST API tests through the transport-independent router (no sockets needed).
#include <unistd.h>  // mkdtemp (macOS declares it here, not in <cstdlib>)

#include <cstdlib>
#include <string>

#include "app_server/api.hpp"
#include "app_server/pairing.hpp"
#include "app_server/relay.hpp"
#include "common/json.hpp"
#include "mini_test.hpp"

using namespace brim;
using namespace brim::app;

struct Fixture {
  std::string dir;
  std::unique_ptr<Db> db;
  Pairing pairing{"123456"};
  ServerState state;
  std::vector<schema::Command> sent;
  std::unique_ptr<Api> api;
  std::string cookie;

  Fixture() {
    char tmpl[] = "/tmp/brim_api_XXXXXX";
    dir = mkdtemp(tmpl);
    std::string err;
    db = Db::open(dir + "/brim.db", std::string(BRIM_REPO_ROOT) + "/data/migrations", &err);
    api = std::make_unique<Api>(*db, pairing, state, [this](const schema::Command& c) { sent.push_back(c); });
  }
  HttpResponse call(const std::string& method, const std::string& path, const std::string& body = "",
                    const std::string& query = "") {
    return api->handle(HttpRequest{method, path, query, body, cookie});
  }
  void pair() {
    auto r = call("POST", "/api/pair", R"({"pin":"123456"})");
    for (auto& h : r.headers)
      if (h.first == "set-cookie:") cookie = h.second.substr(0, h.second.find(';'));
  }
};

TEST_CASE("everything except /api/pair requires pairing") {
  Fixture f;
  for (auto path : {"/api/activity", "/api/alerts", "/api/settings", "/api/health"})
    CHECK_EQ(f.call("GET", path).status, 401);
  CHECK_EQ(f.call("PUT", "/api/settings", "{}").status, 401);
}

TEST_CASE("pairing: wrong PIN 401, right PIN sets HttpOnly cookie") {
  Fixture f;
  CHECK_EQ(f.call("POST", "/api/pair", R"({"pin":"000000"})").status, 401);
  CHECK_EQ(f.call("POST", "/api/pair", "not json").status, 400);
  auto r = f.call("POST", "/api/pair", R"({"pin":"123456"})");
  CHECK_EQ(r.status, 200);
  bool http_only = false;
  for (auto& h : r.headers)
    if (h.first == "set-cookie:" && h.second.find("HttpOnly") != std::string::npos &&
        h.second.find("SameSite=Strict") != std::string::npos)
      http_only = true;
  CHECK(http_only);
  f.pair();
  CHECK_EQ(f.call("GET", "/api/activity").status, 200);
  f.cookie = "brim_session=forged";
  CHECK_EQ(f.call("GET", "/api/activity").status, 401);
}

TEST_CASE("pairing locks after 5 wrong PINs") {
  Fixture f;
  for (int i = 0; i < 4; ++i) CHECK_EQ(f.call("POST", "/api/pair", R"({"pin":"999999"})").status, 401);
  CHECK_EQ(f.call("POST", "/api/pair", R"({"pin":"999999"})").status, 429);
  CHECK_EQ(f.call("POST", "/api/pair", R"({"pin":"123456"})").status, 429);  // even the right PIN
}

TEST_CASE("there is no people API any more") {
  Fixture f;
  f.pair();
  CHECK_EQ(f.call("GET", "/api/people").status, 404);
  CHECK_EQ(f.call("DELETE", "/api/people/1").status, 404);
  CHECK_EQ(f.call("PATCH", "/api/people/1", R"({"display_name":"X"})").status, 404);
  CHECK(f.sent.empty());  // and nothing is asked of brim
}

TEST_CASE("settings: validation, and the reload command carries the settings") {
  Fixture f;
  f.pair();
  auto s = json::parse(f.call("GET", "/api/settings").body);
  REQUIRE(s.has_value());
  s->set("volume", json::Value(150));
  CHECK_EQ(f.call("PUT", "/api/settings", s->dump()).status, 400);
  s->set("volume", json::Value(40));
  s->set("info_min_interval_s", json::Value(5));  // spec: at most one info announcement per 10 s
  CHECK_EQ(f.call("PUT", "/api/settings", s->dump()).status, 400);
  s->set("info_min_interval_s", json::Value(30));
  s->set("announce_obstacles", json::Value(false));
  auto r = f.call("PUT", "/api/settings", s->dump());
  CHECK_EQ(r.status, 200);
  CHECK_EQ(json::parse(r.body)->get_int("volume"), 40);

  REQUIRE(!f.sent.empty());
  const auto& cmd = f.sent.back();
  CHECK(cmd.command == schema::CommandKind::ReloadSettings);
  REQUIRE(cmd.settings.has_value());  // brim applies these without reading SQLite
  CHECK_EQ(cmd.settings->volume, 40);
  CHECK_EQ(cmd.settings->info_min_interval_s, 30);
  CHECK(!cmd.settings->announce_obstacles);
}

TEST_CASE("relay: valid events are stamped and stored, invalid dropped") {
  Fixture f;
  f.pair();
  auto out = process_event(
      R"({"type":"person_seen","seq":1,"source":"brim","t_mono_ms":5,"track_id":4,"direction":"right","proximity":"close","confidence":0.9})",
      *f.db, f.state);
  REQUIRE(!out.empty());
  CHECK(json::parse(out)->find("t_relay_mono_ms") != nullptr);
  out = process_event(
      R"({"type":"obstacle_seen","seq":2,"source":"brim","t_mono_ms":6,"label":"chair","direction":"ahead","proximity":"close","confidence":0.8})",
      *f.db, f.state);
  CHECK(!out.empty());
  out = process_event(
      R"({"type":"clear_path","seq":3,"source":"brim","t_mono_ms":7,"clear":false,"direction":"unknown","text":"Path unclear, stop"})",
      *f.db, f.state);
  CHECK(!out.empty());
  out = process_event(
      R"({"type":"safety_alert","seq":4,"source":"brim","t_mono_ms":8,"kind":"phone_raised","severity":"warning","text":"A phone is pointed at you","direction":"ahead"})",
      *f.db, f.state);
  CHECK(!out.empty());
  // An enrolled person's name goes to the phone live and is never stored.
  out = process_event(
      R"({"type":"safety_alert","seq":5,"source":"brim","t_mono_ms":9,"kind":"person_close","severity":"critical","text":"Person ahead, move left","direction":"ahead","name":"Tiffany"})",
      *f.db, f.state);
  REQUIRE(!out.empty());
  CHECK_EQ(json::parse(out)->get_string("name"), "Tiffany");
  CHECK(f.call("GET", "/api/alerts").body.find("Tiffany") == std::string::npos);
  CHECK(process_event(R"({"type":"person_seen","seq":5})", *f.db, f.state).empty());
  CHECK(process_event(R"({"type":"person_recognized","seq":6,"source":"brim","t_mono_ms":9})", *f.db, f.state).empty());
  CHECK(process_event("garbage", *f.db, f.state).empty());
  CHECK_EQ(f.state.rejected.load(), 3u);

  auto act = json::parse(f.call("GET", "/api/activity", "", "limit=5").body);
  REQUIRE(act && act->as_array().size() == 2);  // person + obstacle; clear_path is live-only
  CHECK_EQ(act->as_array()[0].get_string("kind"), "obstacle");
  CHECK_EQ(act->as_array()[0].get_string("label"), "chair");
  CHECK_EQ(act->as_array()[1].get_string("kind"), "person");
  CHECK(act->as_array()[1].find("display_name") == nullptr);  // no identity reaches the phone
  auto alerts = json::parse(f.call("GET", "/api/alerts").body);
  CHECK_EQ(alerts->as_array().size(), 2u);
  CHECK_EQ(alerts->as_array()[0].get_string("text"), "Person ahead, move left");  // anonymous
  auto health = json::parse(f.call("GET", "/api/health").body);
  CHECK(health->get_bool("brim_connected"));
  CHECK(health->get_bool("db_ok"));
}

TEST_CASE("query_int and cookie parsing") {
  CHECK_EQ(query_int("limit=20&x=1", "limit", 50, 1, 500), 20);
  CHECK_EQ(query_int("x=1&limit=9999", "limit", 50, 1, 500), 500);
  CHECK_EQ(query_int("", "limit", 50, 1, 500), 50);
  CHECK_EQ(session_from_cookie_header("a=1; brim_session=abc; b=2"), "abc");
  CHECK_EQ(session_from_cookie_header("brim_session=xyz"), "xyz");
  CHECK_EQ(session_from_cookie_header("not_brim_session=1"), "");
  CHECK_EQ(random_pin().size(), 6u);
  CHECK(random_token() != random_token());
}

MINI_TEST_MAIN()
