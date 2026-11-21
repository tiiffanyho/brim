# models/

On-device AI models. **Nothing here is downloaded at runtime** (no internet on the hat). Copy the
files here before `scripts/deploy.sh`; large binaries are git-ignored.

**No face models.** Brim does not recognize faces: there is no face detector, no face embedding
model and no identity lookup of any kind. A person is a box with a throw-away track id.

| File (planned) | Used by | Runtime |
|---|---|---|
| `efficientdet_lite0.tflite` (uint8, in use) | `src/detect` — person and object boxes | tflite-runtime 2.21.0 C API (oss.qnx.com) |
| `hand_landmarker.task` | `src/safety` — is that phone being held up at me? | MediaPipe 0.10.26 |
| `depth_estimation.tflite` (TBD) | `src/nav` — free space ahead, left and right | tflite-runtime 2.21.0 |

The camera is the only sensor: there is no distance sensor, so "how close" comes from how much of
the frame a box fills (and later from monocular depth), and every obstacle call is an estimate.

Budget on the Pi 5 CPU: detect ≤ 30 ms, navigation decision ≤ 10 ms, whole frame ≤ 66 ms (15 fps).
Prefer int8-quantized models. Record each model's source + licence here when you add it.

## efficientdet_lite0.tflite — person and object detector

- **Source:** TensorFlow Lite Model Maker export of EfficientDet-Lite0 (COCO 2017), with metadata,
  <https://storage.googleapis.com/download.tensorflow.org/models/tflite/task_library/object_detection/rpi/lite-model_efficientdet_lite0_detection_metadata_1.tflite>
  (also served as `mediapipe-tasks/object_detector/efficientdet_lite0_uint8.tflite`; same bytes).
  Save it as `models/efficientdet_lite0.tflite`.
- **SHA-256:** `2e04c53bfeac0ac2a30c057c7e2a777594ce39baaac35a92f74fb1e8c4fc4e0b` (4,563,519 bytes)
- **Licence:** Apache License 2.0 (TensorFlow / TF Hub `tensorflow/efficientdet/lite0/detection`).
- **Why this file and not `mediapipe-models/.../efficientdet_lite0/int8/1`:** that one has no
  post-processing op, so it outputs raw scores for 19,206 anchors × 90 classes and needs anchor
  decoding + NMS in our code. This export has `TFLite_Detection_PostProcess` (NMS) inside the
  graph, which tflite-runtime's built-in op resolver runs for us.

Tensors (from `scripts/probe_model.py models/efficientdet_lite0.tflite`):

| | Shape | Type | Meaning |
|---|---|---|---|
| input 0 | `[1,320,320,3]` | uint8 (scale 1/128, zero 127) | RGB, raw 0–255 pixels go straight in |
| output 0 | `[1,25,4]` | float32 | boxes `ymin, xmin, ymax, xmax`, 0–1 (can be slightly outside: clamped) |
| output 1 | `[1,25]` | float32 | class: COCO-90 index, 0-based (`labelmap.txt` inside the model zip) |
| output 2 | `[1,25]` | float32 | score |
| output 3 | `[1]` | float32 | number of valid rows |

Classes Brim uses (everything else is ignored): 0 person; 1 bicycle; 2 car, 3 motorcycle, 5 bus,
7 truck → vehicle; 61 chair; 66 dining table → table; 76 cell phone → phone (safety only, never an
obstacle); 14 bench, 26 backpack, 32 suitcase, 62 couch → "something" (never named).
Scores below `min_confidence` (0.60, `config/safety.json`) are dropped; phones use
`phone_min_confidence` (0.55) so the safety module can judge them.

Probe it on the laptop:

```sh
python3 -m venv .dev/venv && .dev/venv/bin/pip install "ai-edge-litert==1.2.0" numpy
.dev/venv/bin/python scripts/probe_model.py models/efficientdet_lite0.tflite
```

### Timing (budget: detect ≤ 30 ms per frame)

brim logs one line every `stats_period_s` (config/detect.json) with avg/max ms for each stage —
resize, invoke, decode, track — against the budget, and `--debug` logs every frame. If the average
goes over `budget_ms`, brim runs the detector on every 2nd frame until it is back under 75 % of
the budget.

| Where | invoke | Notes |
|---|---|---|
| MacBook (Apple silicon, Python, 4 threads) | ~5 ms | probe only, not representative |
| Pi 5, QNX 8.0, 4 threads | **TODO(hw): measure** | read it from brim's `detect timing` log line |

### On-Pi checks before building

```sh
apk info -L tflite-runtime-dev | grep -E 'c_api.h|\.so|\.a$'   # TODO(qnx-check)
```

The detector uses the TFLite **C API** (`tensorflow/lite/c/c_api.h`: `TfLiteModelCreateFromFile`,
`TfLiteInterpreterCreate`, `TfLiteInterpreterInvoke`). CMake looks for that header and a library
named `tensorflowlite_c`, `tensorflow-lite` or `tensorflowlite` under `BRIM_QNX_DEPS_PREFIX`. If
the header is missing, the build still succeeds without detection and brim speaks "Brim cannot
see" at startup. It never pretends the path is clear.
