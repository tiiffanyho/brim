#include "app_server/db.hpp"

#include <dirent.h>
#include <sys/stat.h>
#include <unistd.h>

#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <fstream>
#include <sstream>

#include "common/config.hpp"
#include "common/json.hpp"
#include "common/log.hpp"

namespace brim::app {

std::int64_t now_epoch_ms() {
  timespec ts{};
  clock_gettime(CLOCK_REALTIME, &ts);
  return static_cast<std::int64_t>(ts.tv_sec) * 1000 + ts.tv_nsec / 1000000;
}

std::string iso8601(std::int64_t ms) {
  time_t sec = static_cast<time_t>(ms / 1000);
  tm t{};
  gmtime_r(&sec, &t);
  char buf[40];
  std::snprintf(buf, sizeof buf, "%04d-%02d-%02dT%02d:%02d:%02d.%03dZ", t.tm_year + 1900, t.tm_mon + 1, t.tm_mday, t.tm_hour,
                t.tm_min, t.tm_sec, static_cast<int>(ms % 1000));
  return buf;
}

namespace {

// RAII prepared statement.
class Stmt {
 public:
  Stmt(sqlite3* db, const char* sql) {
    if (sqlite3_prepare_v2(db, sql, -1, &st_, nullptr) != SQLITE_OK) {
      BRIM_LOGE("db", "prepare failed: %s (%s)", sqlite3_errmsg(db), sql);
      st_ = nullptr;
    }
  }
  ~Stmt() { sqlite3_finalize(st_); }
  explicit operator bool() const { return st_ != nullptr; }
  Stmt& bind(int i, std::int64_t v) { sqlite3_bind_int64(st_, i, v); return *this; }
  Stmt& bind(int i, double v) { sqlite3_bind_double(st_, i, v); return *this; }
  Stmt& bind(int i, const std::string& v) { sqlite3_bind_text(st_, i, v.c_str(), -1, SQLITE_TRANSIENT); return *this; }
  Stmt& bind_null(int i) { sqlite3_bind_null(st_, i); return *this; }
  bool step_row() { return st_ && sqlite3_step(st_) == SQLITE_ROW; }
  bool run() { return st_ && sqlite3_step(st_) == SQLITE_DONE; }
  std::int64_t i64(int c) { return sqlite3_column_int64(st_, c); }
  double dbl(int c) { return sqlite3_column_double(st_, c); }
  bool is_null(int c) { return sqlite3_column_type(st_, c) == SQLITE_NULL; }
  std::string text(int c) {
    auto p = sqlite3_column_text(st_, c);
    return p ? reinterpret_cast<const char*>(p) : "";
  }

 private:
  sqlite3_stmt* st_ = nullptr;
};

std::string read_file(const std::string& path) {
  std::ifstream f(path, std::ios::binary);
  std::stringstream ss;
  ss << f.rdbuf();
  return ss.str();
}

}  // namespace

std::unique_ptr<Db> Db::open(const std::string& path, const std::string& migrations_dir, std::string* err) {
  sqlite3* raw = nullptr;
  if (sqlite3_open_v2(path.c_str(), &raw, SQLITE_OPEN_READWRITE | SQLITE_OPEN_CREATE | SQLITE_OPEN_FULLMUTEX, nullptr) != SQLITE_OK) {
    if (err) *err = raw ? sqlite3_errmsg(raw) : "sqlite3_open failed";
    sqlite3_close(raw);
    return nullptr;
  }
  std::unique_ptr<Db> db(new Db(raw));
  sqlite3_busy_timeout(raw, 2000);
  // WAL + synchronous=NORMAL: durable across app crashes, fast, survives power loss up to the last
  // checkpoint. TODO(qnx-check): WAL needs shared-memory mmap; verify on the Pi's filesystem
  // (qnx6/power-safe fs). If it fails, fall back to journal_mode=TRUNCATE.
  db->exec("PRAGMA journal_mode=WAL; PRAGMA synchronous=NORMAL; PRAGMA foreign_keys=ON;");
  if (!db->migrate(migrations_dir, err)) return nullptr;
  return db;
}

Db::~Db() { sqlite3_close(db_); }

bool Db::exec(const std::string& sql, std::string* err) {
  char* msg = nullptr;
  if (sqlite3_exec(db_, sql.c_str(), nullptr, nullptr, &msg) != SQLITE_OK) {
    if (err) *err = msg ? msg : "sqlite error";
    BRIM_LOGE("db", "exec failed: %s", msg ? msg : "?");
    sqlite3_free(msg);
    return false;
  }
  return true;
}

int Db::schema_version() {
  std::lock_guard<std::recursive_mutex> lk(mu_);
  Stmt q(db_, "PRAGMA user_version");
  return q.step_row() ? static_cast<int>(q.i64(0)) : -1;
}

bool Db::ping() {
  std::lock_guard<std::recursive_mutex> lk(mu_);
  Stmt q(db_, "SELECT 1");
  return q.step_row();
}

bool Db::migrate(const std::string& dir, std::string* err) {
  std::vector<std::string> files;
  if (DIR* d = opendir(dir.c_str())) {
    while (dirent* e = readdir(d)) {
      std::string n = e->d_name;
      if (n.size() > 4 && n.compare(n.size() - 4, 4, ".sql") == 0 && std::isdigit(static_cast<unsigned char>(n[0]))) files.push_back(n);
    }
    closedir(d);
  } else {
    if (err) *err = "migrations dir not found: " + dir;
    return false;
  }
  std::sort(files.begin(), files.end());
  int current = schema_version();
  for (const auto& f : files) {
    int v = std::atoi(f.c_str());
    if (v <= current) continue;
    std::string sql = read_file(dir + "/" + f);
    std::string e;
    if (!exec("BEGIN;", &e) || !exec(sql, &e) || !exec("PRAGMA user_version = " + std::to_string(v) + ";", &e) ||
        !exec("COMMIT;", &e)) {
      exec("ROLLBACK;");
      if (err) *err = "migration " + f + " failed: " + e;
      return false;
    }
    BRIM_LOGI("db", "applied migration %s", f.c_str());
    current = v;
  }
  return true;
}

void Db::record_person_seen(std::int64_t track_id, const std::string& direction, const std::string& proximity,
                            double confidence, std::int64_t t_mono_ms, std::int64_t now_ms) {
  std::lock_guard<std::recursive_mutex> lk(mu_);
  Stmt q(db_,
         "INSERT INTO sightings (kind, label, track_id, direction, proximity, confidence, seen_at, seen_epoch_ms, t_mono_ms) "
         "VALUES ('person', NULL, ?, ?, ?, ?, ?, ?, ?)");
  q.bind(1, track_id).bind(2, direction).bind(3, proximity).bind(4, confidence);
  q.bind(5, iso8601(now_ms)).bind(6, now_ms).bind(7, t_mono_ms);
  q.run();
}

void Db::record_obstacle_seen(const std::string& label, std::int64_t track_id, const std::string& direction,
                              const std::string& proximity, double confidence, std::int64_t t_mono_ms, std::int64_t now_ms) {
  std::lock_guard<std::recursive_mutex> lk(mu_);
  Stmt q(db_,
         "INSERT INTO sightings (kind, label, track_id, direction, proximity, confidence, seen_at, seen_epoch_ms, t_mono_ms) "
         "VALUES ('obstacle', ?, ?, ?, ?, ?, ?, ?, ?)");
  q.bind(1, label);
  if (track_id > 0) q.bind(2, track_id); else q.bind_null(2);
  q.bind(3, direction).bind(4, proximity).bind(5, confidence);
  q.bind(6, iso8601(now_ms)).bind(7, now_ms).bind(8, t_mono_ms);
  q.run();
}

std::vector<s::ActivityItem> Db::activity(int limit) {
  std::lock_guard<std::recursive_mutex> lk(mu_);
  std::vector<s::ActivityItem> out;
  Stmt q(db_,
         "SELECT id, kind, label, track_id, direction, proximity, confidence, seen_at "
         "FROM sightings ORDER BY seen_epoch_ms DESC, id DESC LIMIT ?");
  q.bind(1, static_cast<std::int64_t>(limit));
  while (q.step_row()) {
    s::ActivityItem a;
    a.id = q.i64(0);
    if (!s::from_string(q.text(1), a.kind)) continue;
    if (!q.is_null(2)) {
      s::ObstacleKind k;
      if (s::from_string(q.text(2), k)) a.label = k;
    }
    if (!q.is_null(3)) a.track_id = q.i64(3);
    if (!s::from_string(q.text(4), a.direction)) a.direction = s::Direction::Unknown;
    if (!s::from_string(q.text(5), a.proximity)) a.proximity = s::Proximity::Unknown;
    if (!q.is_null(6)) a.confidence = q.dbl(6);
    a.seen_at = q.text(7);
    out.push_back(std::move(a));
  }
  return out;
}

std::int64_t Db::add_alert(const s::SafetyAlertEvent& e, std::int64_t now_ms) {
  std::lock_guard<std::recursive_mutex> lk(mu_);
  Stmt q(db_, "INSERT INTO alerts (kind, severity, text, created_at, created_epoch_ms, t_mono_ms) VALUES (?, ?, ?, ?, ?, ?)");
  q.bind(1, std::string(s::to_string(e.kind))).bind(2, std::string(s::to_string(e.severity))).bind(3, e.text);
  q.bind(4, iso8601(now_ms)).bind(5, now_ms).bind(6, e.t_mono_ms);
  if (!q.run()) return -1;
  return sqlite3_last_insert_rowid(db_);
}

std::vector<s::Alert> Db::alerts(int limit) {
  std::lock_guard<std::recursive_mutex> lk(mu_);
  std::vector<s::Alert> out;
  Stmt q(db_, "SELECT id, kind, severity, text, created_at FROM alerts ORDER BY created_epoch_ms DESC, id DESC LIMIT ?");
  q.bind(1, static_cast<std::int64_t>(limit));
  while (q.step_row()) {
    s::Alert a;
    a.id = q.i64(0);
    if (!s::from_string(q.text(1), a.kind)) continue;
    if (!s::from_string(q.text(2), a.severity)) continue;
    a.text = q.text(3);
    a.created_at = q.text(4);
    out.push_back(std::move(a));
  }
  return out;
}

s::Settings Db::default_settings() { return config::default_settings(); }

s::Settings Db::get_settings() {
  std::lock_guard<std::recursive_mutex> lk(mu_);
  json::Value merged = s::to_json(default_settings());
  Stmt q(db_, "SELECT key, value FROM settings");
  while (q.step_row()) {
    auto v = json::parse(q.text(1));
    if (v && merged.find(q.text(0))) merged.set(q.text(0), *v);
  }
  s::Settings out;
  if (!s::from_json(merged, out)) out = default_settings();  // corrupted row: fail safe
  return out;
}

bool Db::put_settings(const s::Settings& v) {
  std::lock_guard<std::recursive_mutex> lk(mu_);
  json::Value j = s::to_json(v);
  if (!exec("BEGIN;")) return false;
  for (const auto& [k, val] : j.as_object()) {
    Stmt q(db_, "INSERT INTO settings (key, value) VALUES (?, ?) ON CONFLICT(key) DO UPDATE SET value = excluded.value");
    q.bind(1, k).bind(2, val.dump());
    if (!q.run()) {
      exec("ROLLBACK;");
      return false;
    }
  }
  return exec("COMMIT;");
}

void Db::add_session(const std::string& token, std::int64_t now_ms) {
  std::lock_guard<std::recursive_mutex> lk(mu_);
  Stmt q(db_, "INSERT OR REPLACE INTO sessions (token, created_at, last_used_ms) VALUES (?, ?, ?)");
  q.bind(1, token).bind(2, iso8601(now_ms)).bind(3, now_ms);
  q.run();
}

bool Db::check_session(const std::string& token, std::int64_t now_ms) {
  if (token.empty()) return false;
  std::lock_guard<std::recursive_mutex> lk(mu_);
  Stmt q(db_, "UPDATE sessions SET last_used_ms = ? WHERE token = ?");
  q.bind(1, now_ms).bind(2, token);
  return q.run() && sqlite3_changes(db_) == 1;
}

int Db::purge(std::int64_t now_ms, Retention r) {
  std::lock_guard<std::recursive_mutex> lk(mu_);
  int n = 0;
  {
    Stmt q(db_, "DELETE FROM sightings WHERE seen_epoch_ms <= ?");
    q.bind(1, now_ms - r.sightings_ms);
    q.run();
    n += sqlite3_changes(db_);
  }
  {
    Stmt q(db_, "DELETE FROM alerts WHERE created_epoch_ms <= ?");
    q.bind(1, now_ms - r.alerts_ms);
    q.run();
    n += sqlite3_changes(db_);
  }
  return n;
}

}  // namespace brim::app
