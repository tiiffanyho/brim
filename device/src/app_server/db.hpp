// SQLite access layer for app_server. Thread-safe (one connection behind a mutex).
// All writes go through here; brim never touches SQLite in its real-time path.
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
  std::int64_t stranger_ms = 30LL * 60 * 1000;            // privacy rule: 30 minutes
  std::int64_t recognized_ms = 30LL * 24 * 3600 * 1000;   // activity history: 30 days
  std::int64_t alerts_ms = 30LL * 24 * 3600 * 1000;
};

class Db {
 public:
  // Opens/creates the DB and applies migrations from migrations_dir (NNN_*.sql, in order).
  static std::unique_ptr<Db> open(const std::string& path, const std::string& migrations_dir, std::string* err);
  ~Db();

  int schema_version();
  bool ping();

  // people
  std::vector<s::Person> list_people();
  std::optional<s::Person> get_person(std::int64_t id);
  std::int64_t add_person(const std::string& name, const std::string& clip_rel, const std::string& embedding_rel,
                          std::int64_t now_ms = now_epoch_ms());
  std::optional<s::Person> rename_person(std::int64_t id, const std::string& name);
  // Deletes the person's name clip + embedding files (must be inside data_dir), then the row and
  // their sightings. Returns false (and deletes nothing from the DB) if a file couldn't be removed.
  enum class DeleteResult { Deleted, NotFound, FileError };
  DeleteResult delete_person(std::int64_t id, const std::string& data_dir, std::string* err = nullptr);

  // events -> history
  void record_recognized(std::int64_t person_id, const std::string& direction, double confidence, std::int64_t t_mono_ms,
                         std::int64_t now_ms = now_epoch_ms());
  void record_stranger(const std::string& anon_id, const std::string& direction, std::int64_t t_mono_ms,
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

  // Inserts Sam/Priya/Alex (ids 1..3) with placeholder files if the people table is empty.
  void seed_demo(const std::string& data_dir);

 private:
  explicit Db(sqlite3* db) : db_(db) {}
  bool exec(const std::string& sql, std::string* err = nullptr);
  bool migrate(const std::string& dir, std::string* err);
  std::optional<s::Person> get_person_locked(std::int64_t id);

  sqlite3* db_;
  std::recursive_mutex mu_;
};

}  // namespace brim::app
