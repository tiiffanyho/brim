# Brim — project rules

Brim is a hat that keeps a blind or low-vision wearer safe and tells them the safest way to move
(Raspberry Pi 5 + QNX 8.0) plus a phone companion PWA.
Hackathon track: **Best Use of QNX**. Judges score: cannot-fail, real-time/reliability, creative AI,
runs on embedded hardware (not cloud).

## What Brim does, in priority order
- **MUST** Safety: warn when a person or object is close in front, and when a phone is held up
  toward the wearer.
- **MUST** Navigation: after a warning, say the safest direction to move (clear ahead, left or
  right), and say "Path unclear, stop" when no direction is safe.
- **SHOULD** Obstacles: name the obstacle and its side ("chair ahead, step right"). Never name an
  obstacle that was not seen with confidence.
- **SHOULD** People: say where people are ("person on your left, close by") without saying who.
- **COULD** Device state: speak faults out loud ("Camera is blocked", "Brim is restarting").
- **NOT NOW** Remembered places, maps, GPS routes, a rear camera.

## Hard constraints (never break these)
- **No face recognition, no face embeddings, no names, no identity lookup, no stored photos.**
  People are anonymous boxes with a throw-away `track_id`. Boxes live in memory for seconds; track
  data is cleared within 30 minutes. Nothing about a person is ever written to disk.
- Device code must run on **QNX OS 8.0** and use open-source AI modules from https://oss.qnx.com
  (mediapipe 0.10.26, tflite-runtime 2.21.0, OpenCV 4.12; optional whisper.cpp).
- **No cloud services and no internet at runtime.** The only network is the local Wi-Fi link to our
  own phone app. **No recording**: frames live in memory only, never written to disk.
- Hardware is only: Pi 5 8 GB + Active Cooler (QNX 8.0 on microSD), Logitech C270 USB webcam with
  built-in mic (blue USB 3.0 port), USB-C power bank, HDMI monitor for the demo dashboard. **The Pi
  plays no sound**: it sends short text events and the phone app speaks them. **No distance sensor**: the camera is the only way to see an obstacle, so every
  obstacle and proximity call is an estimate from the image (box area fraction). There is no display
  on the hat — the pairing PIN is shown only on the HDMI dashboard (a helper reads it out) and is
  never sent to phones.
- **Never give a direction we are not sure of.** When the path is unclear, say "Path unclear, stop".
- QNX is POSIX but **not Linux**: no V4L2, systemd, apt, epoll, inotify, /proc-parsing or other
  Linux-only APIs in `device/`. If unsure whether an API exists on QNX 8.0, put it behind an
  interface, add a `TODO(qnx-check): how to verify on the Pi`, and keep going.
- The phone app must be fully usable with **VoiceOver and TalkBack** (labels, landmarks, focus
  management, live regions, no colour-only meaning, 44px touch targets).

## Architecture rules
- `shared/events.schema.json` is the single source of truth for every event and API payload.
  After editing it run `node shared/gen/generate.mjs` and commit the regenerated files
  (CI fails if `shared/gen/` is stale). Never hand-edit `shared/gen/events.*`.
  Events: `person_seen`, `obstacle_seen`, `clear_path`, `safety_alert`, `fault`, `health`, and
  `traffic_light_seen` (traffic light feasibility test only: never walk/cross/go, never a direction).
  Don't change the schema or the REST routes without asking first.
- Processes: `brim` (real-time pipeline, high priority), `supervisor` (restarts brim, watchdog),
  `app_server` (lowest priority, REST + WebSocket + static PWA + SQLite). brim must never block on
  app_server: publishing an event is fire-and-forget with a bounded queue.
- brim never reads SQLite. app_server pushes what brim needs as commands: `reload_settings` carries
  the settings. (`speak_pairing_pin` is still in the schema but no longer sent: the PIN never
  leaves app_server and the dashboard.)
- IPC goes through `device/src/ipc/` only (QNX message passing on the Pi, Unix sockets on the laptop).
- Every event carries `t_mono_ms` (CLOCK_MONOTONIC at emit time) so we can measure delay. Log
  capture, detection, decision and speak-start times; fail loudly with a spoken fault, then recover.
- Speech priority: **safety > navigation > info** (people, obstacles). On the Pi, `SpeakQueue`
  sends cue events in that order (faults go with safety); stale nav/info (> 3 s) is dropped and info
  is rate-limited to one per `info_min_interval_s` (≥ 10 s). On the phone, safety_alert goes to the
  assertive live region at once, clear_path as soon as the warning before it has been read, and the
  rest waits for silence. A direction is never spoken before the warning that made it necessary.
- Stubs are marked `TODO(hw)` (needs real hardware), `TODO(qnx-check)` (verify API on QNX),
  `TODO(ai)` (model work). Grep for these before a demo.

## Workflow
- Laptop: `scripts/dev.sh` (no hardware needed). Tests: `ctest --preset host`, `cd app && npm test`.
- Keep `brim --fake`, `scripts/dev.sh`, all tests and `scripts/smoke_test.sh` passing after every
  change.
- Pi: `BRIM_PI_HOST=<ip> scripts/deploy.sh` (needs QNX SDP 8.0 env sourced).
- Keep the device code C++17, no exceptions across module boundaries, no heap churn in the
  per-frame path once real models land.
- Each phase names one branch; commit only on that branch, never on another phase's branch.
