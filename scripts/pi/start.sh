#!/bin/sh
# Runs ON THE PI (QNX 8.0, /bin/sh is ksh). Starts supervisor, which starts brim (priority 20)
# and app_server (priority 8). No systemd on QNX: to start on boot, call this script from the
# image's startup (e.g. /etc/rc.d/rc.local or the IFS build file).
# TODO(qnx-check): confirm `on`, `nohup`, `slay` exist in the Pi image (they're standard QNX utils).
cd "$(dirname "$0")" || exit 1
DIR=$(pwd)
export LD_LIBRARY_PATH="$DIR/lib:$LD_LIBRARY_PATH"
export BRIM_CONFIG_DIR="$DIR/config"
mkdir -p data /tmp/brim

on -p 25 nohup "$DIR/bin/supervisor" \
  --brim "$DIR/bin/brim" \
  --app-server "$DIR/bin/app_server" \
  --app-server-arg --static --app-server-arg "$DIR/app/dist" \
  --app-server-arg --migrations --app-server-arg "$DIR/data/migrations" \
  --app-server-arg --data-dir --app-server-arg "$DIR/data" \
  --hw-watchdog \
  >> /tmp/brim.log 2>&1 &
echo "brim started (log: /tmp/brim.log)"
