#include "app_server/db.hpp"

#include <dirent.h>
#include <sys/stat.h>
#include <unistd.h>

#include <algorithm>
#include <cerrno>
#include <cctype>
#include <climits>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <fstream>
#include <sstream>

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

constexpr const char* kPersonCols = "id, display_name, name_clip_path, embedding_path, enrolled_at, last_seen_at";

s::Person person_from(Stmt& q) {
  s::Person p;
  p.id = q.i64(0);
  p.display_name = q.text(1);
  p.has_name_clip = !q.is_null(2) && !q.text(2).empty();
  p.has_embedding = !q.is_null(3) && !q.text(3).empty();
  p.enrolled_at = q.text(4);
  if (!q.is_null(5)) p.last_seen_at = q.text(5);
  return p;
}

// Deletes data_dir/rel only if it resolves inside data_dir. Missing file = success.
bool remove_inside(const std::string& data_dir, const std::string& rel, std::string* err) {
  if (rel.empty()) return true;
  if (rel[0] == '/' || rel.find("..") != std::string::npos) {
    if (err) *err = "refusing to delete path outside data dir: " + rel;
    return false;
  }
  std::string full = data_dir + "/" + rel;
  char real_dir[PATH_MAX], real_file[PATH_MAX];
  if (!realpath(data_dir.c_str(), real_dir)) {
    if (err) *err = "data dir missing: " + data_dir;
    return false;
  }
  if (!realpath(full.c_str(), real_file)) return errno == ENOENT;  // already gone
  std::string d(real_dir), f(real_file);
  if (f.compare(0, d.size() + 1, d + "/") != 0) {
    if (err) *err = "refusing to delete path outside data dir: " + f;
    return false;
  }
  if (::unlink(real_file) != 0 && errno != ENOENT) {
    if (err) *err = "unlink " + f + ": " + std::strerror(errno);
    return false;
  }
  return true;
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

std::vector<s::Person> Db::list_people() {
  std::lock_guard<std::recursive_mutex> lk(mu_);
  std::vector<s::Person> out;
  std::string sql = std::string("SELECT ") + kPersonCols + " FROM people ORDER BY display_name COLLATE NOCASE";
  Stmt q(db_, sql.c_str());
  while (q.step_row()) out.push_back(person_from(q));
  return out;
}

std::optional<s::Person> Db::get_person_locked(std::int64_t id) {
  std::string sql = std::string("SELECT ") + kPersonCols + " FROM people WHERE id = ?";
  Stmt q(db_, sql.c_str());
  q.bind(1, id);
  if (!q.step_row()) return std::nullopt;
  return person_from(q);
}

std::optional<s::Person> Db::get_person(std::int64_t id) {
  std::lock_guard<std::recursive_mutex> lk(mu_);
  return get_person_locked(id);
}

std::int64_t Db::add_person(const std::string& name, const std::string& clip, const std::string& emb, std::int64_t now_ms) {
  std::lock_guard<std::recursive_mutex> lk(mu_);
  Stmt q(db_, "INSERT INTO people (display_name, name_clip_path, embedding_path, enrolled_at) VALUES (?, ?, ?, ?)");
  q.bind(1, name);
  if (clip.empty()) q.bind_null(2); else q.bind(2, clip);
  if (emb.empty()) q.bind_null(3); else q.bind(3, emb);
  q.bind(4, iso8601(now_ms));
  if (!q.run()) return -1;
  return sqlite3_last_insert_rowid(db_);
}

std::optional<s::Person> Db::rename_person(std::int64_t id, const std::string& name) {
  std::lock_guard<std::recursive_mutex> lk(mu_);
  Stmt q(db_, "UPDATE people SET display_name = ? WHERE id = ?");
  q.bind(1, name).bind(2, id);
  if (!q.run() || sqlite3_changes(db_) == 0) return std::nullopt;
  return get_person_locked(id);
}

Db::DeleteResult Db::delete_person(std::int64_t id, const std::string& data_dir, std::string* err) {
  std::lock_guard<std::recursive_mutex> lk(mu_);
  std::string clip, emb;
  {
    Stmt q(db_, "SELECT name_clip_path, embedding_path FROM people WHERE id = ?");
    q.bind(1, id);
    if (!q.step_row()) return DeleteResult::NotFound;
    clip = q.text(0);
    emb = q.text(1);
  }
  // Files first: if we can't delete the fingerprint, we must not report the person as forgotten.
  if (!remove_inside(data_dir, emb, err) || !remove_inside(data_dir, clip, err)) return DeleteResult::FileError;
  Stmt d(db_, "DELETE FROM people WHERE id = ?");  // sightings cascade
  d.bind(1, id);
  if (!d.run()) {
    if (err) *err = sqlite3_errmsg(db_);
    return DeleteResult::FileError;
  }
  return DeleteResult::Deleted;
}

void Db::record_recognized(std::int64_t person_id, const std::string& direction, double confidence, std::int64_t t_mono_ms,
                           std::int64_t now_ms) {
  std::lock_guard<std::recursive_mutex> lk(mu_);
  std::string at = iso8601(now_ms);
  Stmt q(db_,
         "INSERT INTO sightings (kind, person_id, direction, confidence, seen_at, seen_epoch_ms, t_mono_ms) "
         "SELECT 'recognized', id, ?, ?, ?, ?, ? FROM people WHERE id = ?");
  q.bind(1, direction).bind(2, confidence).bind(3, at).bind(4, now_ms).bind(5, t_mono_ms).bind(6, person_id);
  q.run();
  Stmt u(db_, "UPDATE people SET last_seen_at = ? WHERE id = ?");
  u.bind(1, at).bind(2, person_id);
  u.run();
}

void Db::record_stranger(const std::string& anon_id, const std::string& direction, std::int64_t t_mono_ms, std::int64_t now_ms) {
  std::lock_guard<std::recursive_mutex> lk(mu_);
  Stmt q(db_,
         "INSERT INTO sightings (kind, anon_id, direction, seen_at, seen_epoch_ms, t_mono_ms) VALUES ('stranger', ?, ?, ?, ?, ?)");
  q.bind(1, anon_id).bind(2, direction).bind(3, iso8601(now_ms)).bind(4, now_ms).bind(5, t_mono_ms);
  q.run();
}

std::vector<s::ActivityItem> Db::activity(int limit) {
  std::lock_guard<std::recursive_mutex> lk(mu_);
  std::vector<s::ActivityItem> out;
  Stmt q(db_,
         "SELECT s.id, s.kind, s.person_id, p.display_name, s.anon_id, s.direction, s.seen_at "
         "FROM sightings s LEFT JOIN people p ON p.id = s.person_id ORDER BY s.seen_epoch_ms DESC, s.id DESC LIMIT ?");
  q.bind(1, static_cast<std::int64_t>(limit));
  while (q.step_row()) {
    s::ActivityItem a;
    a.id = q.i64(0);
    s::from_string(q.text(1), a.kind);
    if (!q.is_null(2)) a.person_id = q.i64(2);
    if (!q.is_null(3)) a.display_name = q.text(3);
    if (!q.is_null(4)) a.anon_id = q.text(4);
    if (!s::from_string(q.text(5), a.direction)) a.direction = s::Direction::Unknown;
    a.seen_at = q.text(6);
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

s::Settings Db::default_settings() { return s::Settings{80, 1.0, true, false, s::Sensitivity::Normal, 10}; }

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
    Stmt q(db_, "DELETE FROM sightings WHERE kind = 'stranger' AND seen_epoch_ms <= ?");
    q.bind(1, now_ms - r.stranger_ms);
    q.run();
    n += sqlite3_changes(db_);
  }
  {
    Stmt q(db_, "DELETE FROM sightings WHERE kind = 'recognized' AND seen_epoch_ms <= ?");
    q.bind(1, now_ms - r.recognized_ms);
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

void Db::seed_demo(const std::string& data_dir) {
  std::lock_guard<std::recursive_mutex> lk(mu_);
  {
    Stmt q(db_, "SELECT COUNT(*) FROM people");
    if (q.step_row() && q.i64(0) > 0) return;
  }
  ::mkdir(data_dir.c_str(), 0700);
  ::mkdir((data_dir + "/clips").c_str(), 0700);
  ::mkdir((data_dir + "/embeddings").c_str(), 0700);
  const char* names[] = {"Sam", "Priya", "Alex"};
  for (int i = 0; i < 3; ++i) {
    std::string id = std::to_string(i + 1);
    std::string clip = "clips/" + id + ".wav", emb = "embeddings/" + id + ".bin";
    // Placeholder files so the "forget deletes files" path is real in the demo.
    std::ofstream(data_dir + "/" + clip) << "placeholder name clip";
    std::ofstream(data_dir + "/" + emb) << std::string(128 * 4, '\0');
    Stmt q(db_, "INSERT INTO people (id, display_name, name_clip_path, embedding_path, enrolled_at) VALUES (?, ?, ?, ?, ?)");
    q.bind(1, static_cast<std::int64_t>(i + 1)).bind(2, std::string(names[i])).bind(3, clip).bind(4, emb).bind(5, iso8601(now_epoch_ms()));
    q.run();
  }
  BRIM_LOGI("db", "seeded demo people: Sam, Priya, Alex");
}

}  // namespace brim::app
