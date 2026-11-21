# models/

On-device AI models. **Nothing here is downloaded at runtime** (no internet on the hat). Copy the
files here before `scripts/deploy.sh`; large binaries are git-ignored.

**No face models.** Brim does not recognize faces: there is no face detector, no face embedding
model and no identity lookup of any kind. A person is a box with a throw-away track id.

| File (planned) | Used by | Runtime |
|---|---|---|
| `efficientdet_lite0.tflite` (int8) | `src/detect` — person and object boxes | MediaPipe 0.10.26 / tflite-runtime 2.21.0 (oss.qnx.com) |
| `hand_landmarker.task` | `src/safety` — is that phone being held up at me? | MediaPipe 0.10.26 |
| `depth_estimation.tflite` (TBD) | `src/nav` — free space ahead, left and right | tflite-runtime 2.21.0 |

The camera is the only sensor: there is no distance sensor, so "how close" comes from how much of
the frame a box fills (and later from monocular depth), and every obstacle call is an estimate.

Budget on the Pi 5 CPU: detect ≤ 30 ms, navigation decision ≤ 10 ms, whole frame ≤ 66 ms (15 fps).
Prefer int8-quantized models. Record each model's source + licence here when you add it.
