# Brim

A hat that keeps a blind or low-vision wearer safe and tells them the safest way to move. A
Raspberry Pi 5 running **QNX 8.0** watches through a webcam, warns when a person or object is close
in front, together with which way is safe ("Person ahead, move left", "Chair ahead, move right"),
or "Path unclear, stop" when no way is. It also names obstacles and says where people are, without
saying *who* they are unless that person enrolled. The Pi plays no sound: an iPhone app (`mobile/`)
speaks with the phone's own voice, also with the phone locked, and a web app (`app/`,
VoiceOver/TalkBack-first) shows activity, alerts, settings and device health.

**Everything runs on the hat: no cloud, no internet, no recording. Only people who enrolled are
ever named; everyone else stays "Person".**

> Status: **runs on the real hat.** On the Pi, the Logitech C270 is read through QNX's camera
> library (`libcamapi`) at 15 fps, and EfficientDet-Lite0 runs on `tflite-runtime` from
> oss.qnx.com with XNNPACK (about 75 ms per detection, down from about 360 ms without it).
> Still to do, marked `TODO(hw)` / `TODO(ai)` / `TODO(qnx-check)`: the warning for a phone held up
> toward the wearer, the Pi 5 hardware watchdog (a software watchdog runs for now), and drawing the
> HDMI dashboard (the PIN is in the log and in `scripts/pi/check.sh` meanwhile).

## Privacy, in one paragraph

Brim does not know who a stranger is and never tries to. A person is a box with a throw-away
`track_id` that means "the same person as a moment ago", said as "Person". The one exception,
agreed by the team (Gloriana, Tiffany, Kim): someone who **agreed** can be enrolled
(`scripts/faces/enrol.py <name> <photo>`), and then Brim may say "<name> ahead, move left" when it
is sure it is them. Only their face embedding is kept, in `data/faces/` on the hat — never a photo,
never uploaded, synced or committed — and `scripts/faces/remove.py <name>` deletes it. With nobody
enrolled no face model runs at all. A stranger's face is compared in memory and dropped at once.
Names go to the phone live and are never stored in the database or the logs. Boxes live in memory
for a few seconds; every track is cleared within 30 minutes, and the phone's history is purged on
the same clock. No frames, no audio and no photos are ever written to disk, and the only network
traffic is to our own phone app on the local Wi-Fi. `device/tests/test_privacy.cpp` fails the build
if a name reaches a stored or `person_seen` event, or face data reaches the schema.

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
                     iPhone app (speaks) · web app (Activity · Alerts · Settings · Health)
```

- **`shared/events.schema.json`** is the single source of truth. `node shared/gen/generate.mjs`
  writes `shared/gen/events.ts` (PWA) and `shared/gen/events.gen.hpp` (C++ structs + JSON).
  The six events are `person_seen`, `obstacle_seen`, `clear_path`, `safety_alert`, `fault`,
  `health`.
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
| `scripts/laptop/brim_test.py live --say [--why]` | The hat's detector on the webcam, saying what brim would say (SAYS lines, from brim's own C++ via `device/tools/decide_probe`); names enrolled people. Nothing is saved. |
| `scripts/laptop/brim_test.py live --camera http://<pi-ip>:8090` | The same, on the **hat's** camera: reads frames from a temporary test viewer on the Pi (`camview`, port 8090, not part of Brim, not in the repo). The window says *HAT CAMERA via Pi …*. |
| `cd app && npm run camera` | Laptop camera as a stand-in hat in the browser (dev only); the iPhone app can link to it instead of a hat. |
| `scripts/laptop/object_test.py data/object_test` | Detector accuracy per class (every class Brim maps) on labelled photos; paths in `config/object_test.json`, photos git-ignored. |
| `scripts/laptop/object_live.py` | The same test live on the webcam: point it at things, press a key for what is there, get the per-class table. Untested classes are skipped. Nothing is saved. |
| `cd device && ctest --preset host` | C++ unit tests: schema, detection geometry, privacy, speak queue, SQLite layer, REST API. |
| `cd app && npm test && npm run lint && npm run typecheck` | PWA tests, jsx-a11y lint, types. |

Dev data lives in `.dev/data` (git-ignored). Delete it to reset.

## iPhone app

```bash
cd mobile && npm ci && npx expo start --dev-client --host lan
```

Debug builds load the app's screens from this server each time they open, so keep it running (same
Wi-Fi as the phone). For a demo, install a Release build (`npx expo run:ios --device --configuration
Release`), which needs no laptop. Pair with the hat's address (e.g. `172.20.10.14`) and the PIN.
See `mobile/README.md`.

## Set up on the Pi itself (no laptop toolchain)

```bash
cd ~/brim && sh scripts/pi/setup_pi.sh      # installs from oss.qnx.com, builds, tests, lays out ~/brim-run
sudo sh ~/brim-run/start.sh                 # supervisor -> brim + app_server   (stop: sudo sh ~/brim-run/stop.sh)
sh scripts/pi/check.sh                      # processes, camera fps, faults, PIN, chip temperature
sudo sh scripts/pi/autostart.sh             # start Brim on every boot (off: ... autostart.sh off)
```

Plug the webcam into a blue USB 3.0 port before powering on.

## Deploy to the Pi from a laptop

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
  third_party/tflite/      vendored XNNPACK delegate header (TensorFlow v2.21.0, Apache 2.0)
app/          Vite + React + TS PWA: src/api, src/screens, src/a11y; public/ manifest + service worker
mobile/       Expo / React Native iPhone app: onboarding, Live, Settings; speaks with expo-speech
data/migrations/  versioned SQLite schema (PRAGMA user_version)
config/       safety.json, nav.json, detect.json, faces.json, app.json
models/       on-device models (see models/README.md)
scripts/      dev.sh, deploy.sh, smoke_test.sh, fake_events.py, probe_model.py
  pi/         setup_pi.sh, start.sh, stop.sh, check.sh, autostart.sh, collect.c (labelled test photos)
  laptop/     brim_test.py, object_test.py, object_live.py
  faces/      enrol.py, remove.py, list.py
RULES.md      the team's project rules
```

## REST API (all under `/api`, cookie required except `/pair`)

`POST /pair` · `GET /health` · `GET /activity?limit=` · `GET /alerts?limit=` · `GET|PUT /settings` ·
WebSocket `/ws` (subprotocol `brim-events`).

There is no `/api/people`: enrolled people are managed only on the hat itself with
`scripts/faces/{enrol,remove,list}.py`, never over the network. `PUT /settings` saves the settings and sends them to brim in the `reload_settings` command.

## Safety and privacy rules (enforced in code + tests)

- Only enrolled people (who agreed) are named, only on a sure match, and only as "<name> ahead,
  move left|right" or "<name> ahead, stop". Everyone else is "Person". Face embeddings stay in
  `data/faces/` on the hat; names are never stored or logged; nothing runs while nobody is enrolled.
- A `track_id` is forgotten within 30 minutes, and the same person coming back later is a new,
  unrelated id. Phone history is purged on the same 30-minute clock.
- No frames, audio or photos are written to disk, ever.
- Safety comes first, then navigation: a direction is never spoken before the warning that caused
  it, and an unclear path is spoken as "Path unclear, stop" rather than a guess.
- An obstacle is never named unless it was seen with confidence; otherwise it is just "something".
- If detection is down, brim says so out loud and reports a `fault` — it never implies the path is
  clear.
