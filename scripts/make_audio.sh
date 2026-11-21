#!/usr/bin/env bash
# Renders the fixed phrases the hat speaks into audio/*.wav using macOS `say` (offline).
# Format: 16 kHz, 16-bit little-endian mono WAV — easy to play with QNX io-snd / libasound.
# TODO(hw): confirm the earpiece sounds OK at 16 kHz; bump to 22050 if speech is muffled.
set -euo pipefail
cd "$(dirname "$0")/.."
command -v say >/dev/null || { echo "needs macOS 'say'"; exit 1; }
VOICE=${BRIM_VOICE:-Samantha}
RATE=${BRIM_RATE:-185}
mkdir -p audio

# file_name|phrase   (stub list — extend freely. Nobody is named: there are no per-person clips.)
PHRASES=$(cat <<'LIST'
safety_person_close|Person close ahead
safety_object_close|Something close ahead
safety_phone_raised|A phone is pointed at you
safety_stop|Stop
nav_clear_ahead|Clear path ahead
nav_step_left|Step left
nav_step_right|Step right
nav_path_unclear|Path unclear, stop
info_person|Person
info_obstacle|Something
info_chair|Chair
info_table|Table
info_door|Door
info_pole|Pole
info_stairs|Stairs
info_vehicle|Vehicle
info_bicycle|Bicycle
dir_left|on your left
dir_right|on your right
dir_ahead|ahead
prox_close|close by
prox_near|nearby
system_ready|Brim is ready
system_restarting|Restarting the camera
system_recovered|Working again
system_cannot_see|Brim cannot see. Stop and ask for help
system_paired|Phone paired
system_pin|Pairing code
LIST
)
for d in 0 1 2 3 4 5 6 7 8 9; do PHRASES+=$'\n'"digit_$d|$d"; done

while IFS='|' read -r name text; do
  [[ -z "$name" ]] && continue
  say -v "$VOICE" -r "$RATE" -o "audio/$name.wav" --file-format=WAVE --data-format=LEI16@16000 "$text"
  echo "audio/$name.wav  \"$text\""
done <<< "$PHRASES"
