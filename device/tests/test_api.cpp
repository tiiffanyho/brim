// REST API tests through the transport-independent router (no sockets needed).
#include <cstdlib>
#include <string>
#include <sys/stat.h>
#include <unistd.h>

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
    db->seed_demo(dir);
    api = std::make_unique<Api>(*db, pairing, state, dir, [this](const schema::Command& c) { sent.push_back(c); });
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
  for (auto path : {"/api/people", "/api/activity", "/api/alerts", "/api/settings", "/api/health"})
    CHECK_EQ(f.call("GET", path).status, 401);
  CHECK_EQ(f.call("DELETE", "/api/people/1").status, 401);
  CHECK(f.db->get_person(1).has_value());
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
  CHECK_EQ(f.call("GET", "/api/people").status, 200);
  f.cookie = "brim_session=forged";
  CHECK_EQ(f.call("GET", "/api/people").status, 401);
}

TEST_CASE("pairing locks after 5 wrong PINs") {
  Fixture f;
  for (int i = 0; i < 4; ++i) CHECK_EQ(f.call("POST", "/api/pair", R"({"pin":"999999"})").status, 401);
  CHECK_EQ(f.call("POST", "/api/pair", R"({"pin":"999999"})").status, 429);
  CHECK_EQ(f.call("POST", "/api/pair", R"({"pin":"123456"})").status, 429);  // even the right PIN
}

TEST_CASE("people: list, rename, delete removes files and tells brim") {
  Fixture f;
  f.pair();
  auto list = json::parse(f.call("GET", "/api/people").body);
  REQUIRE(list && list->is_array());
  CHECK_EQ(list->as_array().size(), 3u);

  auto r = f.call("PATCH", "/api/people/1", R"({"display_name":"  Samantha "})");
  CHECK_EQ(r.status, 200);
  CHECK_EQ(json::parse(r.body)->get_string("display_name"), "Samantha");
  CHECK_EQ(f.call("PATCH", "/api/people/1", R"({"display_name":"   "})").status, 400);
  CHECK_EQ(f.call("PATCH", "/api/people/99", R"({"display_name":"X"})").status, 404);
  CHECK_EQ(f.call("PATCH", "/api/people/abc", R"({"display_name":"X"})").status, 400);

  struct stat st;
  CHECK(::stat((f.dir + "/embeddings/1.bin").c_str(), &st) == 0);
  CHECK_EQ(f.call("DELETE", "/api/people/1").status, 204);
  CHECK(::stat((f.dir + "/embeddings/1.bin").c_str(), &st) != 0);
  CHECK(::stat((f.dir + "/clips/1.wav").c_str(), &st) != 0);
  CHECK_EQ(f.call("DELETE", "/api/people/1").status, 404);
  REQUIRE(!f.sent.empty());
  CHECK(f.sent.back().command == schema::CommandKind::ForgetPerson);
  CHECK(f.sent.back().person_id && *f.sent.back().person_id == 1);
}

TEST_CASE("settings: validation and reload command") {
  Fixture f;
  f.pair();
  auto s = json::parse(f.call("GET", "/api/settings").body);
  REQUIRE(s.has_value());
  s->set("volume", json::Value(150));
  CHECK_EQ(f.call("PUT", "/api/settings", s->dump()).status, 400);
  s->set("volume", json::Value(40));
  s->set("cue_min_interval_s", json::Value(5));  // spec: at most one cue per 10 s
  CHECK_EQ(f.call("PUT", "/api/settings", s->dump()).status, 400);
  s->set("cue_min_interval_s", json::Value(30));
  auto r = f.call("PUT", "/api/settings", s->dump());
  CHECK_EQ(r.status, 200);
  CHECK_EQ(json::parse(r.body)->get_int("volume"), 40);
  CHECK(f.sent.back().command == schema::CommandKind::ReloadSettings);
}

TEST_CASE("relay: valid events are stamped and stored, invalid dropped") {
  Fixture f;
  f.pair();
  auto out = process_event(
      R"({"type":"person_recognized","seq":1,"source":"brim","t_mono_ms":5,"person_id":2,"display_name":"Priya","confidence":0.9,"direction":"right"})",
      *f.db, f.state);
  REQUIRE(!out.empty());
  CHECK(json::parse(out)->find("t_relay_mono_ms") != nullptr);
  out = process_event(
      R"({"type":"safety_alert","seq":2,"source":"brim","t_mono_ms":6,"kind":"vehicle","severity":"critical","text":"Car","direction":"left"})",
      *f.db, f.state);
  CHECK(!out.empty());
  CHECK(process_event(R"({"type":"cue","seq":3})", *f.db, f.state).empty());
  CHECK(process_event("garbage", *f.db, f.state).empty());
  CHECK_EQ(f.state.rejected.load(), 2u);

  auto act = json::parse(f.call("GET", "/api/activity", "", "limit=5").body);
  REQUIRE(act && act->as_array().size() == 1);
  CHECK_EQ(act->as_array()[0].get_string("display_name"), "Priya");
  auto alerts = json::parse(f.call("GET", "/api/alerts").body);
  CHECK_EQ(alerts->as_array().size(), 1u);
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
