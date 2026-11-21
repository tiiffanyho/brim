// REST API, independent of libwebsockets so it can be unit-tested (tests/test_api.cpp).
//
//   POST   /api/pair            {pin}            -> 200 + Set-Cookie | 401 | 429   (no auth)
//   GET    /api/health                           -> HealthResponse
//   GET    /api/people                           -> Person[]
//   PATCH  /api/people/:id      PersonPatch      -> Person
//   DELETE /api/people/:id                       -> 204 (deletes name clip + embedding files)
//   GET    /api/activity?limit=N                 -> ActivityItem[]
//   GET    /api/alerts?limit=N                   -> Alert[]
//   GET    /api/settings                         -> Settings
//   PUT    /api/settings        Settings         -> Settings
// Every route except /api/pair requires the session cookie. Errors are ApiError JSON.
#pragma once

#include <functional>
#include <string>
#include <utility>
#include <vector>

#include "app_server/db.hpp"
#include "app_server/pairing.hpp"
#include "app_server/state.hpp"

namespace brim::app {

struct HttpRequest {
  std::string method;  // GET POST PUT PATCH DELETE
  std::string path;    // without query string
  std::string query;   // "limit=20&x=y"
  std::string body;
  std::string cookie_header;
};

struct HttpResponse {
  int status = 200;
  std::string content_type = "application/json";
  std::string body;
  std::vector<std::pair<std::string, std::string>> headers;
};

class Api {
 public:
  using CommandSink = std::function<void(const schema::Command&)>;
  Api(Db& db, Pairing& pairing, ServerState& state, std::string data_dir, CommandSink send_command)
      : db_(db), pairing_(pairing), state_(state), data_dir_(std::move(data_dir)), send_(std::move(send_command)) {}

  HttpResponse handle(const HttpRequest& req);
  bool authorized(const std::string& cookie_header);

 private:
  HttpResponse pair(const HttpRequest& req);
  HttpResponse health();
  HttpResponse people_item(const HttpRequest& req, std::int64_t id);
  HttpResponse settings(const HttpRequest& req);

  Db& db_;
  Pairing& pairing_;
  ServerState& state_;
  std::string data_dir_;
  CommandSink send_;
};

// Query helper: returns the integer value of `key` clamped to [lo, hi], or def.
int query_int(const std::string& query, const std::string& key, int def, int lo, int hi);

}  // namespace brim::app
