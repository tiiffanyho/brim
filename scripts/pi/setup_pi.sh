#!/bin/sh
# setup_pi.sh : one-step setup + build of Brim ON THE PI (QNX 8.0), no QNX SDP or laptop toolchain.
#
# Run it from the repo folder on the Pi:
#     cd ~/brim && sh scripts/pi/setup_pi.sh
#
# What it does, in order:
#   1. Makes hotspot downloads reliable (smaller network packets: MTU 1280).
#   2. Installs the build tools + libraries from oss.qnx.com with apk (skips what's already there).
#   3. Builds the C++ device code natively on the Pi with cmake (the Pi has its own compiler).
#   4. Runs the unit tests.
#   5. Lays out a ready-to-run folder at ~/brim-run (bin, config, migrations, app, models, audio).
#   6. Tells you the exact command to start Brim.
# It stops with a clear message at the first real problem. Safe to run again.

set -u                                   # treat unset variables as errors (catches typos)

REPO=$(cd "$(dirname "$0")/../.." && pwd)  # the repo folder (two levels up from scripts/pi)
BUILD="$REPO/device/build/pi"             # where the compiled programs go
RUN="$HOME/brim-run"                      # the folder Brim runs from
JOBS=4                                    # the Pi 5 has 4 CPU cores

say()  { printf '\n\033[1;36m[setup]\033[0m %s\n' "$*"; }      # step headings in cyan
warn() { printf '\033[1;33m[warn]\033[0m %s\n' "$*"; }          # problems we can live with
die()  { printf '\033[1;31m[stop]\033[0m %s\n' "$*"; exit 1; }  # problems we can't

# --- 1. Network --------------------------------------------------------------------------------
say "1/6 Making downloads reliable on the hotspot (MTU 1280; resets on reboot)"
sudo ifconfig bcm0 mtu 1280 2>/dev/null || warn "couldn't set MTU (not on Wi-Fi?) - continuing"
curl -s -I https://repo.oss.qnx.com >/dev/null 2>&1 \
  || warn "can't reach oss.qnx.com - if packages are missing, check the hotspot and run this again"

# --- 2. Packages -------------------------------------------------------------------------------
# install_one NAME...: tries each candidate name in turn and installs the first that exists.
# (Package names on oss.qnx.com follow Alpine Linux naming; the alternatives cover small differences.)
install_one() {
  for name in "$@"; do
    if apk info -e "$name" >/dev/null 2>&1; then echo "  ok   $name (already installed)"; return 0; fi
    if apk search -x "$name" 2>/dev/null | grep -q .; then
      if sudo apk add "$name" >/dev/null 2>&1; then echo "  ok   $name (installed)"; return 0; fi
      warn "apk add $name failed (stalled download? run the script again)"
    fi
  done
  echo "  MISSING one of: $*"
  return 1
}

say "2/6 Installing build tools and libraries (apk, from oss.qnx.com)"
sudo apk update >/dev/null 2>&1 || warn "apk update failed - using the package list we already have"

MISSING_CORE=0
install_one cmake                      || MISSING_CORE=1   # builds the C++ project
install_one make ninja                 || MISSING_CORE=1   # runs the actual compile steps
install_one qnx-sf-base-dev            || MISSING_CORE=1   # QNX camera headers (camera/camera_api.h)

APP_SERVER=ON                                              # app_server talks to the phone app
install_one sqlite-dev sqlite3-dev libsqlite3-dev         || APP_SERVER=OFF
install_one libwebsockets-dev libwebsockets              || APP_SERVER=OFF
install_one libuv-dev                                     || APP_SERVER=OFF  # libwebsockets links libuv (found on the Pi)

# For the AI detector (code person 1). Not needed to build today's code, so only warn.
install_one tflite-runtime-dev || warn "tflite-runtime-dev missing: the detector can't be built yet"

[ "$MISSING_CORE" = 0 ] || die "a core build tool is missing (see MISSING above). Install it and run this script again."

# The detector's model file is git-ignored (models/README.md has its source, licence and checksum).
# Download it once (about 4.5 MB) and check it's exactly the expected file.
MODEL="$REPO/models/efficientdet_lite0.tflite"
MODEL_URL="https://storage.googleapis.com/download.tensorflow.org/models/tflite/task_library/object_detection/rpi/lite-model_efficientdet_lite0_detection_metadata_1.tflite"
MODEL_SHA="2e04c53bfeac0ac2a30c057c7e2a777594ce39baaac35a92f74fb1e8c4fc4e0b"
MODEL_BYTES=4563519
if [ ! -s "$MODEL" ]; then
  echo "  downloading the detector model (EfficientDet-Lite0, Apache 2.0)..."
  curl -fsSL -o "$MODEL" "$MODEL_URL" || { rm -f "$MODEL"; warn "model download failed: the detector will report a fault until models/efficientdet_lite0.tflite exists"; }
fi
if [ -s "$MODEL" ]; then
  SIZE=$(wc -c < "$MODEL" | tr -d ' ')
  SUM=$( (sha256sum "$MODEL" 2>/dev/null || openssl dgst -sha256 "$MODEL" 2>/dev/null) | tr ' ' '\n' | grep -E '^[0-9a-f]{64}$' | head -1)
  if [ "$SIZE" != "$MODEL_BYTES" ] || { [ -n "$SUM" ] && [ "$SUM" != "$MODEL_SHA" ]; }; then
    rm -f "$MODEL"; warn "the model file was wrong (size $SIZE); deleted it. Run this script again."
  else
    echo "  ok   model efficientdet_lite0.tflite ($SIZE bytes${SUM:+, checksum matches})"
  fi
fi
[ "$APP_SERVER" = ON ] || warn "sqlite/libwebsockets dev packages missing: building WITHOUT app_server (no phone app yet)"

# --- 3. Compile --------------------------------------------------------------------------------
say "3/6 Checking the compiler"
CXX_BIN=$(command -v c++ || command -v clang++ || command -v g++ || true)
[ -n "$CXX_BIN" ] || die "no C++ compiler found (looked for c++, clang++, g++)"
echo "  using $CXX_BIN"
[ -f /usr/include/camera/camera_api.h ] || die "camera headers not found at /usr/include/camera (qnx-sf-base-dev)"

say "4/6 Building Brim on the Pi (a few minutes)"
# Generator: CMake's default is "Unix Makefiles" (make is installed); use Ninja only if make isn't.
GEN_NAME="Unix Makefiles"; command -v make >/dev/null 2>&1 || GEN_NAME="Ninja"
cmake -S "$REPO/device" -B "$BUILD" -G "$GEN_NAME" \
      -DCMAKE_BUILD_TYPE=RelWithDebInfo \
      -DCMAKE_CXX_COMPILER="$CXX_BIN" \
      -DBRIM_BUILD_TESTS=ON \
      -DBRIM_BUILD_APP_SERVER="$APP_SERVER" \
  || die "cmake configure failed. See the lines above."
cmake --build "$BUILD" -j "$JOBS" || die "build failed. See the first 'error:' lines above."

say "5/6 Running the unit tests"
(cd "$BUILD" && ctest --output-on-failure) || warn "some tests failed (see above) - Brim may still run; tell the team"

# --- 4. Lay out the run folder -----------------------------------------------------------------
say "6/6 Laying out $RUN"
mkdir -p "$RUN/bin" "$RUN/config" "$RUN/data/migrations" "$RUN/app" "$RUN/models" "$RUN/audio"
# QNX won't overwrite a program file while it runs ("Resource busy"), so stop Brim first.
if sudo pidin 2>/dev/null | grep -q "brim-run/bin/supervisor" && [ -f "$RUN/stop.sh" ]; then
  echo "  stopping the running Brim so its programs can be replaced"
  sudo sh "$RUN/stop.sh" >/dev/null 2>&1
  sleep 1
fi
cp "$BUILD/brim" "$BUILD/supervisor" "$RUN/bin/" || die "brim/supervisor binaries missing after the build"
[ -f "$BUILD/app_server" ] && cp "$BUILD/app_server" "$RUN/bin/"
cp "$REPO"/config/*.json "$RUN/config/"
cp "$REPO"/data/migrations/*.sql "$RUN/data/migrations/"
cp "$REPO/scripts/pi/start.sh" "$REPO/scripts/pi/stop.sh" "$RUN/"
cp -R "$REPO/models/." "$RUN/models/" 2>/dev/null || true
cp -R "$REPO/audio/." "$RUN/audio/" 2>/dev/null || true
if [ -d "$REPO/app/dist" ]; then
  rm -rf "$RUN/app/dist" && cp -R "$REPO/app/dist" "$RUN/app/dist"
else
  warn "app/dist not found: the phone app won't be served (build it on a laptop: cd app && npm run build)"
fi

# The camera service must be running for the real camera.
sudo pidin 2>/dev/null | grep -q "bin/sensor" || warn "camera service (sensor) isn't running - reboot the Pi, then try again"

say "Done. Start Brim with:"
echo "    sudo sh $RUN/start.sh"
echo "Then check it with:"
echo "    sh $REPO/scripts/pi/check.sh"
