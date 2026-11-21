// SQLite access layer for app_server. Thread-safe (one connection behind a mutex).
// All writes go through here; brim never touches SQLite in its real-time path.
//
// What is stored: anonymous sightings (what, which side, how close, when), safety alerts, the
// wearer's settings and paired-phone tokens. What is never stored: frames, audio, names, face data
// or anything else that could identify a person. Sightings are purged 30 minutes after they happen.
#pragma once

#include <sqlite3.h>

#include <cstdint>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <vector>

#include "events.gen.hpp"

namespace brim::app {

namespace s = brim::schema;

std::int64_t now_epoch_ms();
std::string iso8601(std::int64_t epoch_ms);

struct Retention {
  std::int64_t sightings_ms = 30LL * 60 * 1000;           // privacy rule: 30 minutes, people and objects alike
  std::int64_t alerts_ms = 30LL * 24 * 3600 * 1000;       // safety history (no identity in it) kept 30 days
};

class Db {
 public:
  // Opens/creates the DB and applies migrations from migrations_dir (NNN_*.sql, in order).
  static std::unique_ptr<Db> open(const std::string& path, const std::string& migrations_dir, std::string* err);
  ~Db();

  int schema_version();
  bool ping();

  // events -> history (anonymous)
  void record_person_seen(std::int64_t track_id, const std::string& direction, const std::string& proximity, double confidence,
                          std::int64_t t_mono_ms, std::int64_t now_ms = now_epoch_ms());
  void record_obstacle_seen(const std::string& label, std::int64_t track_id, const std::string& direction,
                            const std::string& proximity, double confidence, std::int64_t t_mono_ms,
                            std::int64_t now_ms = now_epoch_ms());
  std::vector<s::ActivityItem> activity(int limit);
  std::int64_t add_alert(const s::SafetyAlertEvent& e, std::int64_t now_ms = now_epoch_ms());
  std::vector<s::Alert> alerts(int limit);

  // settings
  s::Settings get_settings();
  bool put_settings(const s::Settings& v);
  static s::Settings default_settings();

  // pairing sessions
  void add_session(const std::string& token, std::int64_t now_ms = now_epoch_ms());
  bool check_session(const std::string& token, std::int64_t now_ms = now_epoch_ms());

  // Privacy/retention purge. Returns number of rows deleted.
  int purge(std::int64_t now_ms = now_epoch_ms(), Retention r = {});

 private:
  explicit Db(sqlite3* db) : db_(db) {}
  bool exec(const std::string& sql, std::string* err = nullptr);
  bool migrate(const std::string& dir, std::string* err);

  sqlite3* db_;
  std::recursive_mutex mu_;
};

}  // namespace brim::app
