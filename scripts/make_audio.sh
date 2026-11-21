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

# file_name|phrase   (stub list — extend freely; names come from enrollment, not here)
PHRASES=$(cat <<'LIST'
safety_head_height|Low obstacle, head height
safety_obstacle_ahead|Obstacle ahead
safety_step|Step down ahead
safety_fast_approach|Something is coming fast
safety_vehicle|Vehicle nearby
safety_stop|Stop
name_is_here|is here
cue_waving|is waving
cue_smiling|is smiling at you
cue_approaching|is coming toward you
cue_leaving|is leaving
cue_looking|is looking at you
dir_left|on your left
dir_right|on your right
dir_ahead|ahead
stranger_nearby|Someone you don't know is nearby
system_ready|Brim is ready
system_restarting|Restarting the camera
system_recovered|Working again
system_paired|Phone paired
system_forgot|Forgotten
system_pin|Pairing code
LIST
)
for d in 0 1 2 3 4 5 6 7 8 9; do PHRASES+=$'\n'"digit_$d|$d"; done

while IFS='|' read -r name text; do
  [[ -z "$name" ]] && continue
  say -v "$VOICE" -r "$RATE" -o "audio/$name.wav" --file-format=WAVE --data-format=LEI16@16000 "$text"
  echo "audio/$name.wav  \"$text\""
done <<< "$PHRASES"
