# Brim

A hat for blind and low-vision people. A Raspberry Pi 5 running **QNX 8.0** recognizes the people
you've enrolled ("Sam is here"), notices social cues (waving, smiling), warns about head-height
obstacles and steps, and speaks through an earpiece. A phone PWA (VoiceOver/TalkBack-first)
manages people, shows activity, alerts and device health. **Everything runs on the hat: no cloud,
no internet, no video recording.**

> Status: **skeleton + pipelines**. All plumbing works end to end with fake data; camera, AI
> models and audio are stubs marked `TODO(hw)`, `TODO(ai)` and `TODO(qnx-check)`.

## Architecture

```
 ┌──────────────────────────── Raspberry Pi 5 · QNX 8.0 ─────────────────────────────┐
 │  supervisor (prio 25) ── heartbeat pulses every 200 ms, restart < 2 s, watchdog   │
 │     │ spawns                                                                      │
 │     ▼                                                                             │
 │  brim (prio 20)                                                                   │
 │   capture → faces → recognize → cues / safety ──► SpeakQueue ──► audio (earpiece)  │
 │                                     │  safety > name > cue, cues wait for silence │
 │                                     ▼                                             │
 │                       EventBus (QNX MsgSend / pulses)                             │
 │                                     ▼                                             │
 │  app_server (prio 8): validate → SQLite → WebSocket / REST / static PWA (lws)     │
 └─────────────────────────────────────┬─────────────────────────────────────────────┘
                                       │ local Wi-Fi only, PIN pairing + HttpOnly cookie
                                   Phone PWA (People · Activity · Alerts · Settings · Health)
```

- **`shared/events.schema.json`** is the single source of truth. `node shared/gen/generate.mjs`
  writes `shared/gen/events.ts` (PWA) and `shared/gen/events.gen.hpp` (C++ structs + JSON).
- **IPC** (`device/src/ipc/`): one interface, two backends chosen at build time. On QNX it uses
  `name_attach`/`MsgSendv`/`MsgSendPulse`; on the laptop it uses Unix datagram sockets.
  brim never blocks on app_server: publishing goes into a bounded queue that drops the oldest event when full.
- **Every event carries `t_mono_ms`** (CLOCK_MONOTONIC). app_server stamps `t_relay_mono_ms`, and
  Device health shows the camera→event and internal relay delays.

## Run it on a laptop (no hardware)

Prereqs (macOS): `brew install cmake libwebsockets sqlite node`. On Linux, install `cmake`,
`libwebsockets-dev`, `libsqlite3-dev`, `libssl-dev` and Node 22.

```bash
scripts/dev.sh
```

That generates the types, builds and unit-tests the device code, then starts app_server, the
supervisor with `brim --fake`, and Vite. It prints a URL and a **6-digit PIN**. Open the URL (or
the LAN URL on your phone, on the same Wi-Fi) and enter the PIN. You'll see "Sam is here",
cues, safety alerts and health arriving live. The screen reader hears them through ARIA live regions.

Things to try:

| | |
|---|---|
| `kill -USR1 $(pgrep -x brim)` | Freezes brim's pipeline. The supervisor restarts it in about 1.3 s, and the phone shows *restarting → recovered*. |
| `scripts/fake_events.py` | Sends extra random events straight into app_server (handy for PWA-only work). |
| `scripts/smoke_test.sh` | Full end-to-end check (also runs in CI). |
| `cd device && ctest --preset host` | C++ unit tests: schema, SQLite layer, speak queue, privacy, REST API. |
| `cd app && npm test && npm run lint && npm run typecheck` | PWA tests, jsx-a11y lint, types. |

Dev data lives in `.dev/data` (git-ignored). Delete it to reset the demo people.

## Deploy to the Pi

```bash
source ~/qnx800/qnxsdp-env.sh                 # QNX SDP 8.0 (qcc/q++)
export BRIM_QNX_DEPS_PREFIX=~/qnx-oss         # libwebsockets 4.5.8, sqlite 3.51.0 (+ AI libs) from oss.qnx.com
BRIM_PI_HOST=192.168.1.42 scripts/deploy.sh   # BRIM_PI_USER (root), BRIM_PI_DIR (/data/brim)
```

`deploy.sh` cross-compiles with the `qnx-aarch64le` preset and builds the PWA. It copies the
binaries, `app/dist`, config, migrations, models and audio over scp, then runs `scripts/pi/start.sh`
over ssh. On the Pi, open `http://<pi-ip>:8080` and pair with the PIN printed in `/tmp/brim/pairing.txt`.

## Repo layout

```
shared/       events.schema.json, gen/ (generator + generated TS/C++), examples/ (test fixtures)
device/       C++17 + CMake. Presets: host, qnx-aarch64le (cmake/qnx-aarch64le.cmake)
  src/common  json, logging, timing/deadlines, config, sysinfo, event stamping
  src/ipc     EventBus: bus_qnx.cpp (Pi) | bus_uds.cpp (laptop)
  src/capture src/faces src/recognize src/cues src/safety src/audio   pipeline modules (stubs + interfaces)
  src/brim_main.cpp        pipeline wiring + --fake scenario
  src/supervisor/          heartbeat, restart, fault events, watchdog interface
  src/app_server/          lws server, REST router (api.cpp), relay, SQLite (db.cpp), pairing
  src/dashboard/           hat display stub (shows the PIN)
  tests/                   dependency-free unit tests (also buildable for QNX)
app/          Vite + React + TS PWA: src/api, src/screens, src/a11y; public/ manifest + service worker
data/migrations/  versioned SQLite schema (PRAGMA user_version)
config/       cues.json (thresholds), app.json (paths, ports, rates)
models/ audio/    on-device models and pre-rendered speech (see their READMEs)
scripts/      dev.sh, deploy.sh, smoke_test.sh, fake_events.py, make_audio.sh, pi/start.sh, pi/stop.sh
```

## REST API (all under `/api`, cookie required except `/pair`)

`POST /pair` · `GET /health` · `GET /people` · `PATCH|DELETE /people/:id` · `GET /activity?limit=` ·
`GET /alerts?limit=` · `GET|PUT /settings` · WebSocket `/ws` (subprotocol `brim-events`).
`DELETE /people/:id` deletes the embedding and name clip (only inside the data dir) before
removing the row, and tells brim to drop the embedding from memory.

## Privacy rules (enforced in code + tests)

- Only enrolled people have names. Strangers get a temporary `stranger-N` id. They are forgotten
  in brim's memory and purged from SQLite 30 minutes after they were last seen.
- No frames are ever written to disk.
- Forgetting a person deletes their fingerprint and voice clip, and their sighting history.
- The phone never announces strangers.
