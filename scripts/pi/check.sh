#!/bin/sh
# check.sh : is Brim healthy on the Pi? Run after start.sh:
#     sh ~/brim/scripts/pi/check.sh
# Prints the running processes, the camera frame rate, faults, the phone pairing PIN and the
# chip temperature, with a plain-English verdict for each.

LOG=/tmp/brim.log
ok()   { printf '\033[1;32m  OK  \033[0m %s\n' "$*"; }
bad()  { printf '\033[1;31m  !!  \033[0m %s\n' "$*"; }

echo "== Processes =="
# sudo: Brim and the camera service run as root, and plain `pidin` only lists your own programs.
PROCS=$(sudo pidin 2>/dev/null)
for p in supervisor brim app_server sensor; do
  if echo "$PROCS" | grep -E -q "bin/$p( |$)"; then ok "$p is running"; else bad "$p is NOT running"; fi
done

echo "== Camera =="
if [ ! -f "$LOG" ]; then
  bad "no log yet at $LOG (did start.sh run?)"
else
  CAM=$(grep "camera=" "$LOG" | tail -1)
  case "$CAM" in
    *camera=qnx-camapi*) ok "brim is using the REAL camera" ;;
    *camera=fake*)       bad "brim is using the FAKE camera" ;;
    *)                   bad "brim hasn't reported its camera yet (wait 5 s and run again)" ;;
  esac
  FPS=$(grep "\[capture\].*fps (frame" "$LOG" | tail -1)
  if [ -n "$FPS" ]; then ok "latest: $FPS"; else bad "no frame-rate line yet (wait 10 s and run again)"; fi
  ERR=$(grep -E "ERROR|camera_open|no frame from camera|module init failed" "$LOG" | tail -3)
  [ -n "$ERR" ] && { echo "  recent problems:"; echo "$ERR" | sed 's/^/    /'; }
fi

echo "== Phone app =="
if [ -s /tmp/brim/pairing.txt ]; then
  ok "pairing PIN: $(head -1 /tmp/brim/pairing.txt)  (open http://spatialpi.local:8080 on the hotspot)"
else
  bad "no pairing PIN yet (app_server not running or still starting)"
fi

echo "== Temperature =="
T=$(cat /dev/thermal 2>/dev/null)
if [ -n "$T" ]; then ok "chip at $T C (fine below ~80)"; else bad "couldn't read /dev/thermal"; fi

echo
echo "Full log: tail -f $LOG   (control+C to stop watching)"
