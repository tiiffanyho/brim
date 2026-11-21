# Brim — project rules

Brim is a hat for blind and low-vision people (Raspberry Pi 5 + QNX 8.0) plus a phone companion PWA.
Hackathon track: **Best Use of QNX**. Judges score: cannot-fail, real-time/reliability, creative AI,
runs on embedded hardware (not cloud).

## Hard constraints (never break these)
- Device code must run on **QNX OS 8.0** and use open-source AI modules from https://oss.qnx.com
  (mediapipe 0.10.26, tflite-runtime 2.21.0, OpenCV 4.12; optional whisper.cpp).
- **No cloud services and no internet at runtime.** Data never leaves the Pi. **No video recording**
  (frames live in memory only, never written to disk).
- Privacy: only enrolled people have names; strangers are anonymous and **forgotten after 30 minutes**;
  "forget <name>" deletes a person's fingerprint (embedding) and voice clip.
- QNX is POSIX but **not Linux**: no V4L2, systemd, apt, epoll, inotify, /proc-parsing or other
  Linux-only APIs in `device/`. If unsure whether an API exists on QNX 8.0, put it behind an
  interface, add a `TODO(qnx-check): how to verify on the Pi`, and keep going.
- The phone app must be fully usable with **VoiceOver and TalkBack** (labels, landmarks, focus
  management, live regions, no colour-only meaning, 44px touch targets).

## Architecture rules
- `shared/events.schema.json` is the single source of truth for every event and API payload.
  After editing it run `node shared/gen/generate.mjs` and commit the regenerated files
  (CI fails if `shared/gen/` is stale). Never hand-edit `shared/gen/events.*`.
- Processes: `brim` (real-time pipeline, high priority), `supervisor` (restarts brim, watchdog),
  `app_server` (lowest priority, REST + WebSocket + static PWA + SQLite). brim must never block on
  app_server: publishing an event is fire-and-forget with a bounded queue.
- IPC goes through `device/src/ipc/` only (QNX message passing on the Pi, Unix sockets on the laptop).
- Every event carries `t_mono_ms` (CLOCK_MONOTONIC at emit time) so we can measure delay.
- Speech priority: safety > name > cue. Cues wait for silence, expire after 3 s, max one per 10 s.
- Stubs are marked `TODO(hw)` (needs real hardware), `TODO(qnx-check)` (verify API on QNX),
  `TODO(ai)` (model work). Grep for these before a demo.

## Workflow
- Laptop: `scripts/dev.sh` (no hardware needed). Tests: `ctest --preset host`, `cd app && npm test`.
- Pi: `BRIM_PI_HOST=<ip> scripts/deploy.sh` (needs QNX SDP 8.0 env sourced).
- Keep the device code C++17, no exceptions across module boundaries, no heap churn in the
  per-frame path once real models land.
