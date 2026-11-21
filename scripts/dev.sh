#!/usr/bin/env bash
# Laptop dev loop — no hardware needed.
#   1. regenerate schema types   2. build + unit-test device code (host preset)
#   3. app_server (REST/WS/SQLite)   4. supervisor -> brim --fake
#   5. Vite dev server for the PWA (proxies /api and /ws to app_server)
# Open the printed URL, enter the printed PIN (on the Pi it is on the HDMI dashboard),
# and watch safety warnings, directions, people, obstacles and health arrive.
#
# Env: BRIM_PORT (app_server, default 8080)  BRIM_VITE_PORT (default 5173)
#      BRIM_RUN_DIR (IPC sockets, default /tmp/brim-$USER)  BRIM_SKIP_TESTS=1
set -euo pipefail
cd "$(dirname "$0")/.."
ROOT=$PWD
PORT=${BRIM_PORT:-8080}
VITE_PORT=${BRIM_VITE_PORT:-5173}
export BRIM_RUN_DIR=${BRIM_RUN_DIR:-/tmp/brim-${USER:-dev}}   # keep short: socket paths max ~104 bytes
export BRIM_CONFIG_DIR=$ROOT/config
DEV=$ROOT/.dev
B=$ROOT/device/build/host

say() { printf '\033[1;33m[dev]\033[0m %s\n' "$*"; }

for tool in cmake node npm; do
  command -v $tool >/dev/null || { echo "missing $tool (macOS: brew install cmake node libwebsockets sqlite)"; exit 1; }
done

say "generating shared types from shared/events.schema.json"
node shared/gen/generate.mjs >/dev/null

say "building device code (host preset)"
(cd device && cmake --preset host >/dev/null && cmake --build --preset host -j 2>&1 | grep -E "error|warning:|Error" || true)
[[ -x $B/brim && -x $B/app_server && -x $B/supervisor ]] || { echo "device build failed"; exit 1; }
if [[ -z "${BRIM_SKIP_TESTS:-}" ]]; then
  say "running device unit tests"
  (cd device && ctest --preset host --output-on-failure | tail -3)
fi

if [[ ! -d app/node_modules ]]; then
  say "installing PWA dependencies"
  (cd app && npm install --no-audit --no-fund)
fi

if lsof -iTCP:"$PORT" -sTCP:LISTEN >/dev/null 2>&1; then
  echo "port $PORT is busy (another dev.sh?). Set BRIM_PORT=... or stop it."; exit 1
fi

mkdir -p "$DEV/data" "$BRIM_RUN_DIR"
rm -f "$BRIM_RUN_DIR/pairing.txt"
PIDS=()
cleanup() {
  trap - INT TERM EXIT
  say "stopping"
  for p in "${PIDS[@]}"; do kill -INT "$p" 2>/dev/null || true; done
  sleep 1
  for p in "${PIDS[@]}"; do kill -KILL "$p" 2>/dev/null || true; done
}
trap cleanup INT TERM EXIT

say "starting app_server on :$PORT (data in .dev/data)"
"$B/app_server" --port "$PORT" --data-dir "$DEV/data" --migrations data/migrations --static app/dist &
PIDS+=($!)
for _ in $(seq 50); do [[ -s $BRIM_RUN_DIR/pairing.txt ]] && break; sleep 0.1; done

say "starting supervisor -> brim --fake"
"$B/supervisor" --brim "$B/brim" --brim-arg --fake &
PIDS+=($!)

say "starting Vite on :$VITE_PORT"
(cd app && BRIM_APP_SERVER="http://127.0.0.1:$PORT" exec npx vite --host 0.0.0.0 --port "$VITE_PORT" --strictPort --clearScreen false) &
PIDS+=($!)

LAN_IP=$(ipconfig getifaddr en0 2>/dev/null || hostname -I 2>/dev/null | awk '{print $1}' || true)
sleep 2
PIN=$(head -1 "$BRIM_RUN_DIR/pairing.txt" 2>/dev/null || echo "?")
cat <<MSG

  ┌──────────────────────────────────────────────────────────┐
     Brim dev is running
     Laptop:  http://localhost:$VITE_PORT
     Phone:   http://${LAN_IP:-<laptop-ip>}:$VITE_PORT   (same Wi-Fi)
     PIN:     $PIN   (on the Pi: HDMI dashboard only; the phone does the talking)
     Demo:    kill -USR1 \$(pgrep -x brim)   -> supervisor restarts it
     Extra:   scripts/fake_events.py        -> more random events
     Ctrl-C stops everything.
  └──────────────────────────────────────────────────────────┘

MSG
wait
