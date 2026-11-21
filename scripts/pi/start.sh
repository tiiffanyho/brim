#!/bin/sh
# Runs ON THE PI (QNX 8.0, /bin/sh is ksh). Starts supervisor, which starts brim (priority 20)
# and app_server (priority 8). Run it with sudo so the supervisor may use priority 25:
#     sudo sh ~/brim-run/start.sh
# No systemd on QNX: to start on boot, call this script from the image's startup
# (e.g. /etc/rc.d/rc.local or the IFS build file).
cd "$(dirname "$0")" || exit 1
DIR=$(pwd)
export LD_LIBRARY_PATH="$DIR/lib:${LD_LIBRARY_PATH:-}"
export BRIM_CONFIG_DIR="$DIR/config"
mkdir -p data /tmp/brim

# Already running? Stop it first so we never run two copies.
if pidin 2>/dev/null | grep -q "bin/supervisor"; then
  echo "brim is already running; restarting it"
  sh "$DIR/stop.sh"
fi

# app_server is optional: setup_pi.sh skips it if sqlite/libwebsockets weren't available.
APP_ARGS=""
if [ -x "$DIR/bin/app_server" ]; then
  APP_ARGS="--app-server $DIR/bin/app_server --app-server-arg --static --app-server-arg $DIR/app/dist \
--app-server-arg --migrations --app-server-arg $DIR/data/migrations --app-server-arg --data-dir --app-server-arg $DIR/data"
else
  echo "note: no app_server binary, starting brim without the phone app"
fi

# `on -p 25` runs the supervisor at QNX priority 25 (above normal work). `nohup` keeps it running
# after you close the SSH window. Fall back gracefully if either utility is missing.
LAUNCH=""
command -v on >/dev/null 2>&1 && LAUNCH="on -p 25"
command -v nohup >/dev/null 2>&1 && LAUNCH="$LAUNCH nohup"

# shellcheck disable=SC2086  (APP_ARGS and LAUNCH are intentionally split into words)
$LAUNCH "$DIR/bin/supervisor" --brim "$DIR/bin/brim" $APP_ARGS --hw-watchdog >> /tmp/brim.log 2>&1 &

echo "brim started (log: /tmp/brim.log). Check it with: sh ~/brim/scripts/pi/check.sh"
