# models/

On-device AI models. **Nothing here is downloaded at runtime** (no internet on the hat). Copy the
files here before `scripts/deploy.sh`; large binaries are git-ignored.

| File (planned) | Used by | Runtime |
|---|---|---|
| `blaze_face_short_range.tflite` | `src/faces` — face detection | MediaPipe 0.10.26 / tflite-runtime 2.21.0 (oss.qnx.com) |
| `face_embedding_128.tflite` (e.g. MobileFaceNet) | `src/recognize` — 128-d face "fingerprint" | tflite-runtime 2.21.0 |
| `hand_landmarker.task`, `face_landmarker.task` | `src/cues` — waving, smiling, looking at you | MediaPipe 0.10.26 |
| `depth_or_obstacle.tflite` (TBD) | `src/safety` — curbs, head-height obstacles | tflite-runtime 2.21.0 |
| `ggml-tiny.en.bin` (optional) | voice command "forget <name>" | whisper.cpp (oss.qnx.com) |

Budget on the Pi 5 CPU: face detect ≤ 15 ms, embed ≤ 10 ms per new track, whole frame ≤ 66 ms (15 fps).
Prefer int8-quantized models. Record each model's source + licence here when you add it.
