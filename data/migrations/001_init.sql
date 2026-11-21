-- 001: initial schema. Applied by app_server (device/src/app_server/db.cpp) in filename order;
-- the applied version is stored in PRAGMA user_version. Never edit an applied migration — add 002_*.sql.
-- Privacy: no images, no video, no audio of strangers. Only enrolled people have names/files.

CREATE TABLE people (
  id              INTEGER PRIMARY KEY,
  display_name    TEXT NOT NULL CHECK (length(display_name) BETWEEN 1 AND 60),
  name_clip_path  TEXT,          -- relative to data_dir, e.g. clips/1.wav  (their name, spoken)
  embedding_path  TEXT,          -- relative to data_dir, e.g. embeddings/1.bin (face fingerprint)
  enrolled_at     TEXT NOT NULL, -- ISO-8601 UTC
  last_seen_at    TEXT
);

CREATE TABLE sightings (
  id            INTEGER PRIMARY KEY,
  kind          TEXT NOT NULL CHECK (kind IN ('recognized', 'stranger')),
  person_id     INTEGER REFERENCES people(id) ON DELETE CASCADE,
  anon_id       TEXT,
  direction     TEXT NOT NULL,
  confidence    REAL,
  seen_at       TEXT NOT NULL,     -- ISO-8601 UTC
  seen_epoch_ms INTEGER NOT NULL,  -- for retention purges
  t_mono_ms     INTEGER,           -- device monotonic timestamp of the event
  CHECK ((kind = 'recognized' AND person_id IS NOT NULL AND anon_id IS NULL)
      OR (kind = 'stranger'   AND person_id IS NULL     AND anon_id IS NOT NULL))
);
CREATE INDEX sightings_by_time ON sightings(seen_epoch_ms);

CREATE TABLE alerts (
  id               INTEGER PRIMARY KEY,
  kind             TEXT NOT NULL,
  severity         TEXT NOT NULL CHECK (severity IN ('info', 'warning', 'critical')),
  text             TEXT NOT NULL,
  created_at       TEXT NOT NULL,
  created_epoch_ms INTEGER NOT NULL,
  t_mono_ms        INTEGER
);
CREATE INDEX alerts_by_time ON alerts(created_epoch_ms);

-- One row per setting; value is JSON. Defaults live in code (db.cpp) so a missing row is safe.
CREATE TABLE settings (
  key   TEXT PRIMARY KEY,
  value TEXT NOT NULL
);

-- Paired phones. TODO: store a hash of the token instead of the token itself.
CREATE TABLE sessions (
  token         TEXT PRIMARY KEY,
  created_at    TEXT NOT NULL,
  last_used_ms  INTEGER NOT NULL
);
