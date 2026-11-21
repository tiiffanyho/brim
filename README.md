# Brim

A hat that keeps a blind or low-vision wearer safe and tells them the safest way to move. A
Raspberry Pi 5 running **QNX 8.0** watches through a webcam, warns when a person or object is close
in front ("person close ahead") or when a phone is pointed at the wearer, then says which way is
safe ("step right") — or "Path unclear, stop" when nothing is. It also names obstacles and says
where people are, without ever saying *who* they are. A phone PWA (VoiceOver/TalkBack-first) shows
activity, alerts, settings and device health.

**Everything runs on the hat: no cloud, no internet, no recording, and no face recognition.**

> Status: **skeleton + pipelines**. All plumbing works end to end with fake data; camera and AI
> models are stubs marked `TODO(hw)`, `TODO(ai)` and `TODO(qnx-check)`. The Pi plays no sound:
> the phone app speaks.

## Privacy, in one paragraph

Brim does not know who anyone is and cannot learn. There is no face detector, no face embedding, no
gallery and no names anywhere in the code, the schema or the database — a person is a box with a
throw-away `track_id` that means "the same person as a moment ago" and nothing more. Boxes live in
memory for a few seconds; every track is cleared within 30 minutes, and the phone's history is
purged on the same clock. No frames, no audio and no photos are ever written to disk, and the only
network traffic is to our own phone app on the local Wi-Fi. `device/tests/test_privacy.cpp` fails
the build if an identity field reappears in the schema.

## Architecture

```
 ┌──────────────────────────── Raspberry Pi 5 · QNX 8.0 ─────────────────────────────┐
 │  supervisor (prio 25) ── heartbeat pulses every 200 ms, restart < 2 s, watchdog   │
 │     │ spawns                                                                      │
 │     ▼                                                                             │
 │  brim (prio 20)                                                                   │
 │   capture → detect (people + objects, anon tracks) → safety → nav                 │
 │                                     │           └──► SpeakQueue (cues) ──┐        │
 │                                     │               safety > nav > info  │        │
 │                                     ▼                                             │
 │                       EventBus (QNX MsgSend / pulses)                             │
 │                                     ▼                                             │
 │  app_server (prio 8): validate → SQLite → WebSocket / REST / static PWA (lws)     │
 │                       ──commands──► brim: reload_settings                         │
 └─────────────────────────────────────┬─────────────────────────────────────────────┘
                                       │ local Wi-Fi only, PIN pairing + HttpOnly cookie
                                 Phone PWA (Activity · Alerts · Settings · Health)
```

- **`shared/events.schema.json`** is the single source of truth. `node shared/gen/generate.mjs`
  writes `shared/gen/events.ts` (PWA) and `shared/gen/events.gen.hpp` (C++ structs + JSON).
  The events are `person_seen`, `obstacle_seen`, `clear_path`, `safety_alert`, `fault`,
  `health`, plus `traffic_light_seen` for the traffic light feasibility test (off by default).
- **The camera is the only sensor.** There is no distance sensor, so "close" is estimated from how
  much of the frame a box fills (`area_frac` → `proximity`), and `clear_path` only names a direction
  when it is sure; otherwise it says *Path unclear, stop*.
- **IPC** (`device/src/ipc/`): one interface, two backends chosen at build time. On QNX it uses
  `name_attach`/`MsgSendv`/`MsgSendPulse`; on the laptop it uses Unix datagram sockets.
  brim never blocks on app_server: publishing goes into a bounded queue that drops the oldest event when full.
- **brim never reads SQLite.** app_server sends it the saved settings (and re-sends them after a
  restart). The pairing PIN stays in app_server and is shown only on the HDMI dashboard — there is
  no display on the hat, and the Pi plays no sound.
- **Every event carries `t_mono_ms`** (CLOCK_MONOTONIC). app_server stamps `t_relay_mono_ms`, and
  Device health shows the camera→event and internal relay delays.

## Run it on a laptop (no hardware)

Prereqs (macOS): `brew install cmake libwebsockets sqlite node`. On Linux, install `cmake`,
`libwebsockets-dev`, `libsqlite3-dev`, `libssl-dev` and Node 22.

```bash
scripts/dev.sh
```

That generates the types, builds and unit-tests the device code, then starts app_server, the
supervisor with `brim --fake`, and Vite. It prints a URL and a **6-digit PIN** (on the Pi it is on the
HDMI dashboard). Open the URL (or the LAN URL on your
phone, on the same Wi-Fi) and enter the PIN. You'll see safety warnings, directions, people,
obstacles and health arriving live. The phone speaks through two ARIA live regions read by
VoiceOver / TalkBack: safety alerts in the assertive one (they interrupt), everything else in the
polite one. If health events stop for 3 s the phone says "Connection lost", then "Connected again".

Things to try:

| | |
|---|---|
| `kill -USR1 $(pgrep -x brim)` | Freezes brim's pipeline. The supervisor restarts it in about 1.3 s, and the phone shows *restarting → recovered*. |
| `scripts/fake_events.py` | Sends extra random events straight into app_server (handy for PWA-only work). |
| `scripts/smoke_test.sh` | Full end-to-end check (also runs in CI). |
| `scripts/laptop/brim_test.py photo.jpg` | The hat's detector on photos (same model, input and outputs). |
| `scripts/test_traffic.py` | Traffic light feasibility test on labelled photos in `data/traffic_test/` (git-ignored); `--self-test` checks the colour check alone. Lab only, never near roads. |
| `cd device && ctest --preset host` | C++ unit tests: schema, detection geometry, privacy, speak queue, SQLite layer, REST API. |
| `cd app && npm test && npm run lint && npm run typecheck` | PWA tests, jsx-a11y lint, types. |

Dev data lives in `.dev/data` (git-ignored). Delete it to reset.

## Deploy to the Pi

```bash
source ~/qnx800/qnxsdp-env.sh                 # QNX SDP 8.0 (qcc/q++)
export BRIM_QNX_DEPS_PREFIX=~/qnx-oss         # libwebsockets 4.5.8, sqlite 3.51.0 (+ AI libs) from oss.qnx.com
BRIM_PI_HOST=192.168.1.42 scripts/deploy.sh   # BRIM_PI_USER (root), BRIM_PI_DIR (/data/brim)
```

`deploy.sh` cross-compiles with the `qnx-aarch64le` preset and builds the PWA. It copies the
binaries, `app/dist`, config, migrations and models over scp, then runs `scripts/pi/start.sh`
over ssh. On the Pi, open `http://<pi-ip>:8080` and pair with the PIN on the HDMI dashboard (it is also
in `/tmp/brim/pairing.txt`).

## Repo layout

```
shared/       events.schema.json, gen/ (generator + generated TS/C++), examples/ (test fixtures)
device/       C++17 + CMake. Presets: host, qnx-aarch64le (cmake/qnx-aarch64le.cmake)
  src/common  json, logging, timing/deadlines, config, sysinfo, event stamping
  src/ipc     EventBus: bus_qnx.cpp (Pi) | bus_uds.cpp (laptop)
  src/capture src/detect src/safety src/nav  pipeline modules
  src/audio   SpeakQueue: cue events to the phone in priority order (the Pi plays no sound)
  src/brim_main.cpp        pipeline wiring + --fake scenario
  src/supervisor/          heartbeat, restart, fault events, watchdog interface
  src/app_server/          lws server, REST router (api.cpp), relay, SQLite (db.cpp), pairing
  src/dashboard/           HDMI demo dashboard stub (shows the PIN)
  tests/                   dependency-free unit tests (also buildable for QNX)
app/          Vite + React + TS PWA: src/api, src/screens, src/a11y; public/ manifest + service worker
data/migrations/  versioned SQLite schema (PRAGMA user_version)
config/       safety.json (close/confidence thresholds), nav.json (clear-path thresholds), app.json,
              traffic.json (traffic light test: colour thresholds, cue off by default)
models/       on-device models (see models/README.md)
scripts/      dev.sh, deploy.sh, smoke_test.sh, fake_events.py, probe_model.py, test_traffic.py,
              laptop/brim_test.py, pi/start.sh, pi/stop.sh
```

`src/nav` (the clear-path decision) is the next phase; until it exists brim says nothing about
direction in the real pipeline rather than guessing one.

## REST API (all under `/api`, cookie required except `/pair`)

`POST /pair` · `GET /health` · `GET /activity?limit=` · `GET /alerts?limit=` · `GET|PUT /settings` ·
WebSocket `/ws` (subprotocol `brim-events`).

There is no `/api/people`: the hat does not recognize anyone, so there is nobody to list, rename or
forget. `PUT /settings` saves the settings and sends them to brim in the `reload_settings` command.

## Safety and privacy rules (enforced in code + tests)

- Nobody is identified. No face detection, no embeddings, no names, no identity lookup.
- A `track_id` is forgotten within 30 minutes, and the same person coming back later is a new,
  unrelated id. Phone history is purged on the same 30-minute clock.
- No frames, audio or photos are written to disk, ever.
- Safety comes first, then navigation: a direction is never spoken before the warning that caused
  it, and an unclear path is spoken as "Path unclear, stop" rather than a guess.
- An obstacle is never named unless it was seen with confidence; otherwise it is just "something".
- If detection is down, brim says so out loud and reports a `fault` — it never implies the path is
  clear.
