#!/usr/bin/env bash
# End-to-end smoke test on the laptop (also run in CI):
#   app_server + supervisor -> brim --fake, then: pairing, the PIN spoken in the earpiece, live
#   WebSocket events (all six types), a forced pipeline hang -> supervisor restart -> fault events,
#   settings pushed to brim, and the REST history with no identity in it.
# Usage: scripts/smoke_test.sh   (after: cmake --preset host && cmake --build --preset host)
set -euo pipefail
cd "$(dirname "$0")/.."
B=device/build/host
PORT=${BRIM_SMOKE_PORT:-8099}
export BRIM_RUN_DIR=/tmp/brim-smoke-$$        # short: Unix socket paths max ~104 bytes
DATA=$(mktemp -d /tmp/brim-smoke-data.XXXXXX)
JAR=$DATA/cookies
FAILED=0
pass() { echo "  ok   $*"; }
fail() { echo "  FAIL $*"; FAILED=1; }
# Waits up to 15 s for a pattern to appear in a log (the processes are racing us).
wait_for() { for _ in $(seq 60); do grep -q "$2" "$1" && return 0; sleep 0.25; done; return 1; }
json() { node -e 'let s="";process.stdin.on("data",d=>s+=d).on("end",()=>{process.stdout.write(String(('"$1"')(JSON.parse(s))))})'; }

cleanup() {
  [[ -n "${SUP_PID:-}" ]] && kill -INT "$SUP_PID" 2>/dev/null || true
  [[ -n "${APP_PID:-}" ]] && kill -INT "$APP_PID" 2>/dev/null || true
  sleep 1
  rm -rf "$BRIM_RUN_DIR" "$DATA"
}
trap cleanup EXIT

$B/app_server --port "$PORT" --data-dir "$DATA" --migrations data/migrations >"$DATA/app.log" 2>&1 &
APP_PID=$!
$B/supervisor --brim $B/brim --brim-arg --fake >"$DATA/sup.log" 2>&1 &
SUP_PID=$!
for _ in $(seq 50); do [[ -s $BRIM_RUN_DIR/pairing.txt ]] && break; sleep 0.1; done
PIN=$(head -1 "$BRIM_RUN_DIR/pairing.txt")
API=http://127.0.0.1:$PORT/api

echo "pairing"
code=$(curl -s -o /dev/null -w '%{http_code}' $API/activity); [[ $code == 401 ]] && pass "unpaired GET /activity -> 401" || fail "unpaired -> $code"
code=$(curl -s -o /dev/null -w '%{http_code}' -X POST -d '{"pin":"000000"}' $API/pair); [[ $code == 401 ]] && pass "wrong PIN -> 401" || fail "wrong PIN -> $code"
code=$(curl -s -c "$JAR" -o /dev/null -w '%{http_code}' -X POST -d "{\"pin\":\"$PIN\"}" $API/pair); [[ $code == 200 ]] && pass "right PIN -> 200 + cookie" || fail "pair -> $code"
grep -q '#HttpOnly_' "$JAR" && pass "cookie is HttpOnly" || fail "cookie not HttpOnly"
COOKIE="brim_session=$(awk '/brim_session/{print $7}' "$JAR")"

echo "the wearer hears the PIN (no display on the hat)"
wait_for "$DATA/sup.log" "SAY \[nav\] \"Pairing code $(echo "$PIN" | sed 's/./& /g;s/ $//')\"" \
  && pass "brim spoke the pairing code, digit by digit" || fail "brim did not speak the PIN"
wait_for "$DATA/app.log" "brim has the pairing PIN" \
  && pass "app_server confirmed brim took the PIN" || fail "PIN handshake not confirmed"

echo "settings reach brim with the settings in the command"
curl -s -b "$JAR" $API/settings >"$DATA/settings.json"
NEW=$(json 's=>{s.volume=55;s.info_min_interval_s=20;s.announce_obstacles=false;return JSON.stringify(s)}' <"$DATA/settings.json")
code=$(curl -s -b "$JAR" -o /dev/null -w '%{http_code}' -X PUT -H 'Content-Type: application/json' -d "$NEW" $API/settings)
[[ $code == 200 ]] && pass "PUT /settings -> 200" || fail "PUT /settings -> $code"
wait_for "$DATA/sup.log" "reload_settings applied (volume=55, rate=1.00, people=on, obstacles=off, info_min_interval=20s)" \
  && pass "brim applied the settings it was sent" || fail "brim did not apply the settings"
code=$(curl -s -b "$JAR" -o /dev/null -w '%{http_code}' -X PUT -H 'Content-Type: application/json' \
  -d "$(json 's=>{s.info_min_interval_s=5;return JSON.stringify(s)}' <"$DATA/settings.json")" $API/settings)
[[ $code == 400 ]] && pass "an interval under 10 s is refused" || fail "bad interval -> $code"

echo "live events over WebSocket (25 s, includes a forced brim hang at 8 s)"
( sleep 8; pkill -USR1 -x brim || true ) &
node --input-type=module -e '
const ws = new WebSocket(process.argv[1], { protocols: ["brim-events"], headers: { cookie: process.argv[2] } });
const seen = {}; let maxDelay = 0;
ws.onmessage = (m) => { const e = JSON.parse(m.data); seen[e.type] = (seen[e.type] || 0) + 1;
  maxDelay = Math.max(maxDelay, e.t_relay_mono_ms - e.t_mono_ms);
  if (e.type === "fault") console.log("    fault:", e.action, e.message);
  if (e.type === "clear_path") console.log("    path:", e.clear ? e.direction : "unclear", "-", e.text); };
ws.onerror = () => { console.log("WSERR"); process.exit(1); };
setTimeout(() => { console.log(JSON.stringify({ seen, maxDelay })); process.exit(0); }, 25000);
' "ws://127.0.0.1:$PORT/ws" "$COOKIE" >"$DATA/ws.out" || true
grep -E "^    (fault|path):" "$DATA/ws.out" | head -8 || true
RES=$(tail -1 "$DATA/ws.out")
echo "    $RES"
for t in person_seen obstacle_seen clear_path safety_alert health fault; do
  echo "$RES" | grep -q "\"$t\"" && pass "received $t" || fail "no $t event"
done
grep -q "Path unclear, stop" "$DATA/ws.out" && pass "says 'Path unclear, stop' instead of guessing" || fail "no path-unclear decision"
grep -q "restarting brim" "$DATA/sup.log" && pass "supervisor restarted hung brim" || fail "no restart"
grep -q "brim recovered" "$DATA/sup.log" && pass "brim recovered" || fail "no recovery"
wait_for "$DATA/app.log" "re-sent the saved settings to brim after restart" \
  && pass "settings re-sent to the restarted brim" || fail "settings not re-sent after restart"

echo "history, with nothing that identifies anyone"
n=$(curl -s -b "$JAR" "$API/activity?limit=100" | json 'a=>a.length')
[[ $n -gt 0 ]] && pass "GET /activity -> $n anonymous sightings" || fail "activity empty"
curl -s -b "$JAR" "$API/activity?limit=100" >"$DATA/activity.json"
grep -qiE '"(display_name|person_id|anon_id|name_clip|embedding)"' "$DATA/activity.json" \
  && fail "activity contains an identity field" || pass "no identity fields in /activity"
n=$(curl -s -b "$JAR" $API/alerts | json 'a=>a.length')
[[ $n -gt 0 ]] && pass "GET /alerts -> $n safety alerts" || fail "alerts empty"
code=$(curl -s -b "$JAR" -o /dev/null -w '%{http_code}' $API/people); [[ $code == 404 ]] && pass "GET /people is gone -> 404" || fail "/people -> $code"

echo "health"
curl -s -b "$JAR" $API/health | node -e 'let s="";process.stdin.on("data",d=>s+=d).on("end",()=>{const h=JSON.parse(s);console.log("    brim_connected="+h.brim_connected+" restart_count="+h.restart_count+" relay_latency_ms="+(h.relay_latency_ms??0).toFixed(2)+" ws_clients="+h.ws_clients)})'

if [[ $FAILED == 0 ]]; then echo "SMOKE TEST PASSED"; else echo "SMOKE TEST FAILED (logs: $DATA kept)"; trap - EXIT; cleanup_keep() { kill -INT $SUP_PID $APP_PID 2>/dev/null || true; }; cleanup_keep; exit 1; fi
