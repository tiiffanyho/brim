#!/usr/bin/env bash
# End-to-end smoke test on the laptop (also run in CI):
#   app_server + supervisor -> brim --fake, then: pairing, REST, live WebSocket events,
#   forced pipeline hang -> supervisor restart -> fault events, DELETE /people deletes files.
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

cleanup() {
  [[ -n "${SUP_PID:-}" ]] && kill -INT "$SUP_PID" 2>/dev/null || true
  [[ -n "${APP_PID:-}" ]] && kill -INT "$APP_PID" 2>/dev/null || true
  sleep 1
  rm -rf "$BRIM_RUN_DIR" "$DATA"
}
trap cleanup EXIT

$B/app_server --port "$PORT" --data-dir "$DATA" --migrations data/migrations --seed-demo >"$DATA/app.log" 2>&1 &
APP_PID=$!
$B/supervisor --brim $B/brim --brim-arg --fake >"$DATA/sup.log" 2>&1 &
SUP_PID=$!
for _ in $(seq 50); do [[ -s $BRIM_RUN_DIR/pairing.txt ]] && break; sleep 0.1; done
PIN=$(head -1 "$BRIM_RUN_DIR/pairing.txt")
API=http://127.0.0.1:$PORT/api

echo "pairing"
code=$(curl -s -o /dev/null -w '%{http_code}' $API/people); [[ $code == 401 ]] && pass "unpaired GET /people -> 401" || fail "unpaired -> $code"
code=$(curl -s -o /dev/null -w '%{http_code}' -X POST -d '{"pin":"000000"}' $API/pair); [[ $code == 401 ]] && pass "wrong PIN -> 401" || fail "wrong PIN -> $code"
code=$(curl -s -c "$JAR" -o /dev/null -w '%{http_code}' -X POST -d "{\"pin\":\"$PIN\"}" $API/pair); [[ $code == 200 ]] && pass "right PIN -> 200 + cookie" || fail "pair -> $code"
grep -q '#HttpOnly_' "$JAR" && pass "cookie is HttpOnly" || fail "cookie not HttpOnly"
COOKIE="brim_session=$(awk '/brim_session/{print $7}' "$JAR")"

echo "REST"
n=$(curl -s -b "$JAR" $API/people | node -e 'let s="";process.stdin.on("data",d=>s+=d).on("end",()=>console.log(JSON.parse(s).length))')
[[ $n == 3 ]] && pass "GET /people -> 3 demo people" || fail "people count $n"

echo "live events over WebSocket (25 s, includes a forced brim hang at 8 s)"
( sleep 8; pkill -USR1 -x brim || true ) &
node --input-type=module -e '
const ws = new WebSocket(process.argv[1], { protocols: ["brim-events"], headers: { cookie: process.argv[2] } });
const seen = {}; let maxDelay = 0;
ws.onmessage = (m) => { const e = JSON.parse(m.data); seen[e.type] = (seen[e.type] || 0) + 1;
  maxDelay = Math.max(maxDelay, e.t_relay_mono_ms - e.t_mono_ms);
  if (e.type === "fault") console.log("    fault:", e.action, e.message); };
ws.onerror = () => { console.log("WSERR"); process.exit(1); };
setTimeout(() => { console.log(JSON.stringify({ seen, maxDelay })); process.exit(0); }, 25000);
' "ws://127.0.0.1:$PORT/ws" "$COOKIE" >"$DATA/ws.out" || true
grep "^    fault:" "$DATA/ws.out" || true
RES=$(tail -1 "$DATA/ws.out")
echo "    $RES"
for t in person_recognized cue safety_alert health fault unknown_person; do
  echo "$RES" | grep -q "\"$t\"" && pass "received $t" || fail "no $t event"
done
grep -q "restarting brim" "$DATA/sup.log" && pass "supervisor restarted hung brim" || fail "no restart"
grep -q "brim recovered" "$DATA/sup.log" && pass "brim recovered" || fail "no recovery"

echo "forget person"
[[ -f $DATA/embeddings/1.bin ]] || fail "seed embedding missing"
code=$(curl -s -b "$JAR" -o /dev/null -w '%{http_code}' -X DELETE $API/people/1); [[ $code == 204 ]] && pass "DELETE /people/1 -> 204" || fail "delete -> $code"
[[ ! -e $DATA/embeddings/1.bin && ! -e $DATA/clips/1.wav ]] && pass "fingerprint + voice clip deleted" || fail "files remain"
grep -q "forgot person 1" "$DATA/sup.log" && pass "brim dropped embedding from memory" || fail "brim not told"

echo "health"
curl -s -b "$JAR" $API/health | node -e 'let s="";process.stdin.on("data",d=>s+=d).on("end",()=>{const h=JSON.parse(s);console.log("    brim_connected="+h.brim_connected+" restart_count="+h.restart_count+" relay_latency_ms="+(h.relay_latency_ms??0).toFixed(2)+" ws_clients="+h.ws_clients)})'

if [[ $FAILED == 0 ]]; then echo "SMOKE TEST PASSED"; else echo "SMOKE TEST FAILED (logs: $DATA kept)"; trap - EXIT; cleanup_keep() { kill -INT $SUP_PID $APP_PID 2>/dev/null || true; }; cleanup_keep; exit 1; fi
