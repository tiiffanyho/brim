#!/usr/bin/env bash
# Cross-compile for the Pi 5 (QNX 8.0), build the PWA, copy everything, restart services.
#
#   source ~/qnx800/qnxsdp-env.sh             # QNX SDP 8.0: sets QNX_HOST / QNX_TARGET
#   export BRIM_QNX_DEPS_PREFIX=~/qnx-oss     # libwebsockets, sqlite (+ AI libs) from oss.qnx.com
#   BRIM_PI_HOST=192.168.1.42 scripts/deploy.sh
#
# Env: BRIM_PI_HOST (required)  BRIM_PI_USER (default root)  BRIM_PI_DIR (default /data/brim)
#      BRIM_SKIP_APP=1 (don't rebuild the PWA)
set -euo pipefail
cd "$(dirname "$0")/.."
: "${BRIM_PI_HOST:?set BRIM_PI_HOST to the Pi's IP address}"
: "${QNX_TARGET:?QNX_TARGET not set: source <sdp>/qnxsdp-env.sh first}"
PI_USER=${BRIM_PI_USER:-root}
PI_DIR=${BRIM_PI_DIR:-/data/brim}    # TODO(qnx-check): pick a writable, persistent path on the Pi image
TARGET="$PI_USER@$BRIM_PI_HOST"
STAGE=.dev/deploy
say() { printf '\033[1;36m[deploy]\033[0m %s\n' "$*"; }

say "checking generated schema files are fresh"
node shared/gen/generate.mjs --check

say "cross-compiling (qnx-aarch64le preset)"
(cd device && cmake --preset qnx-aarch64le && cmake --build --preset qnx-aarch64le -j)
QB=device/build/qnx-aarch64le

if [[ -z "${BRIM_SKIP_APP:-}" ]]; then
  say "building PWA"
  (cd app && npm ci --no-audit --no-fund && npm run build)
fi

say "staging $STAGE"
rm -rf "$STAGE" && mkdir -p "$STAGE"/{bin,lib,config,data/migrations,app,models}
cp $QB/brim $QB/supervisor $QB/app_server "$STAGE/bin/"
cp config/*.json "$STAGE/config/"
cp data/migrations/*.sql "$STAGE/data/migrations/"
cp -R app/dist "$STAGE/app/dist"
cp -R models/. "$STAGE/models/" 2>/dev/null || true
cp scripts/pi/start.sh scripts/pi/stop.sh "$STAGE/"
# Runtime shared libs from oss.qnx.com that the image doesn't already have.
# TODO(qnx-check): `ldd`-equivalent on QNX is `use -i <bin>` / check with `ldd` from the SDP;
#   trim this list to what the Pi image is missing.
if [[ -n "${BRIM_QNX_DEPS_PREFIX:-}" ]]; then
  for lib in libwebsockets.so* libsqlite3.so* libtensorflowlite_c.so* libtensorflow-lite.so*; do
    cp -P "$BRIM_QNX_DEPS_PREFIX"/lib/$lib "$STAGE/lib/" 2>/dev/null || true
  done
fi

say "copying to $TARGET:$PI_DIR"
ssh "$TARGET" "mkdir -p $PI_DIR"
ssh "$TARGET" "sh $PI_DIR/stop.sh 2>/dev/null || true"
scp -rq "$STAGE"/* "$TARGET:$PI_DIR/"

say "restarting services"
ssh "$TARGET" "sh $PI_DIR/start.sh"
sleep 2
ssh "$TARGET" "cat /tmp/brim/pairing.txt 2>/dev/null; tail -5 /tmp/brim.log 2>/dev/null" || true
say "done — open http://$BRIM_PI_HOST:8080 on the phone"
