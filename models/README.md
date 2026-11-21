# models/

On-device AI models. **Nothing here is downloaded at runtime** (no internet on the hat). Copy the
files here before `scripts/deploy.sh`; large binaries are git-ignored.

**Face models only for people who enrolled** (agreed by Gloriana, Tiffany and Kim; see CLAUDE.md).
They never run while nobody is enrolled, and strangers are never named.

| File (planned) | Used by | Runtime |
|---|---|---|
| `efficientdet_lite0.tflite` (uint8, in use) | `src/detect` — person and object boxes | tflite-runtime 2.21.0 C API (oss.qnx.com) |
| `hand_landmarker.task` | `src/safety` — is that phone being held up at me? | MediaPipe 0.10.26 |
| `depth_estimation.tflite` (TBD) | `src/nav` — free space ahead, left and right | tflite-runtime 2.21.0 |
| `face_detection_yunet_2023mar.onnx` | `src/detect/faces` — the face inside a person box | OpenCV 4.12 `FaceDetectorYN` (oss.qnx.com) |
| `face_recognition_sface_2021dec.onnx` | `src/detect/faces` — embedding, matched against enrolled people | OpenCV 4.12 `FaceRecognizerSF` |

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

## Face recognition of enrolled people (YuNet + SFace)

Only for people who agreed and were enrolled with `scripts/faces/enrol.py`. Nothing here runs
while `data/faces/` is empty. Both files are git-ignored (`models/*.onnx`).

- **face_detection_yunet_2023mar.onnx** — OpenCV Zoo, MIT licence.
  <https://github.com/opencv/opencv_zoo/raw/main/models/face_detection_yunet/face_detection_yunet_2023mar.onnx>
  SHA-256 `8f2383e4dd3cfbb4553ea8718107fc0423210dc964f9f4280604804ed2552fa4` (232,589 bytes)
- **face_recognition_sface_2021dec.onnx** — OpenCV Zoo, Apache License 2.0. 112x112 face -> 128-d
  embedding; matched by cosine similarity against `match_threshold` in `config/faces.json` (0.363,
  OpenCV's published threshold for SFace).
  <https://github.com/opencv/opencv_zoo/raw/main/models/face_recognition_sface/face_recognition_sface_2021dec.onnx>
  SHA-256 `0ba9fbfa01b5270c96627c4ef784da859931e02f04419c829e83484087c34e79` (38,696,353 bytes)

Timing (budget: detect <= 30 ms per frame; faces run on their own thread, at most once per person
per second, so they never take frame time):

| Where | YuNet (face in box) | SFace (embedding) | Notes |
|---|---|---|---|
| MacBook (Apple M4, OpenCV 4.12 Python, 4 threads) | 2.1 ms at 320x240 (4.9 ms at 640x480) | 5.6 ms | int8 versions were no faster here (2.0 / 6.1 ms) |
| Pi 5, QNX 8.0 | **TODO(hw): measure** | **TODO(hw)** | brim logs "face check avg/max ms" every 20 checks |
