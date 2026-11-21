-- 002: Brim no longer recognizes faces, so there is nothing to identify anyone with.
-- Drops the enrolled-people table (names, name clips, face fingerprints) and replaces the
-- sightings table with anonymous ones: what was seen, which side, how close, when.
-- Applied by app_server (device/src/app_server/db.cpp) in filename order.
-- Privacy: no images, no video, no audio, no names, no face data. Sightings are purged after
-- 30 minutes (Retention in db.hpp), so this table only ever holds the last half hour.

DROP INDEX IF EXISTS sightings_by_time;
DROP TABLE IF EXISTS sightings;
DROP TABLE IF EXISTS people;

CREATE TABLE sightings (
  id            INTEGER PRIMARY KEY,
  kind          TEXT NOT NULL CHECK (kind IN ('person', 'obstacle')),
  label         TEXT,              -- obstacle type ('chair', 'pole', ... or 'obstacle'); NULL for people
  track_id      INTEGER,           -- throw-away id, stable only while the thing stays in view
  direction     TEXT NOT NULL,     -- left | ahead | right | unknown
  proximity     TEXT NOT NULL,     -- close | near | far | unknown (estimated from the image)
  confidence    REAL,
  seen_at       TEXT NOT NULL,     -- ISO-8601 UTC
  seen_epoch_ms INTEGER NOT NULL,  -- for the 30-minute retention purge
  t_mono_ms     INTEGER,           -- device monotonic timestamp of the event
  CHECK ((kind = 'person' AND label IS NULL) OR (kind = 'obstacle' AND label IS NOT NULL))
);
CREATE INDEX sightings_by_time ON sightings(seen_epoch_ms);

-- The settings keys changed with the feature set (no social cues, no strangers-vs-names).
-- Defaults live in code (device/src/common/config.cpp), so clearing these rows is safe.
DELETE FROM settings WHERE key IN ('cues_enabled', 'announce_strangers', 'cue_min_interval_s');
