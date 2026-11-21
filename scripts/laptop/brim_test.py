#!/usr/bin/env python3
"""Brim's person/object detector on the laptop, on photos.

Same model, same input and same outputs as the hat (device/src/detect/tflite_detector.cpp):
EfficientDet-Lite0 uint8 (models/efficientdet_lite0.tflite), RGB frame stretched to 320x320 with
the same bilinear resize as detect::RgbResizer, outputs boxes / classes / scores / count from the
in-graph TFLite_Detection_PostProcess op. Laptop only: nothing here runs on the hat.

    python3 -m venv .dev/venv
    .dev/venv/bin/pip install "ai-edge-litert==1.2.0" numpy pillow
    .dev/venv/bin/python scripts/laptop/brim_test.py data/traffic_test/IMG_0001.jpg [...]

Live, on the webcam, with what brim would say (one SAYS line per warning or direction):

    .dev/venv/bin/pip install "opencv-python==4.12.0.88"
    .dev/venv/bin/python scripts/laptop/brim_test.py live [--camera 0] [--say]

The words come from brim itself: each frame's boxes go to device/build/host/decide_probe (built by
`cd device && cmake --build --preset host`), which runs brim's own close warning and navigation code
("Person ahead, move left", "Chair ahead, move right", "Person ahead, stop", "Path unclear, stop").
--say speaks them on the Mac (on the hat the phone speaks; the Pi plays no sound).
If somebody is enrolled (scripts/faces/enrol.py), the face in each person box is checked and a
warning about an enrolled person is said with their name ("Tiffany ahead, move left"), matched by
brim's own C++ (decide_probe). Nobody enrolled: no face model runs. Nothing is saved.

As a library (scripts/test_traffic.py):  model = load_model();  dets = detect(model, load_image(p))
detect() returns RAW model rows (COCO-90 class index, score, box): no Brim class mapping and no
threshold, so a test can read any class directly (e.g. 9 = traffic light).

Privacy: photos are only read. Nothing is written, uploaded or kept, and no face model exists.
"""
from __future__ import annotations

import argparse
import json
import os
import shutil
import subprocess
import sys
import time
from dataclasses import dataclass

import numpy as np

REPO = os.path.abspath(os.path.join(os.path.dirname(__file__), "..", ".."))
DEFAULT_MODEL = os.path.join(REPO, "models", "efficientdet_lite0.tflite")

# COCO-90 labelmap indices (0-based, as the model's labelmap.txt) for the classes Brim reads.
COCO_NAMES = {
    0: "person", 1: "bicycle", 2: "car", 3: "motorcycle", 5: "bus", 7: "truck", 9: "traffic light",
    14: "bench", 26: "backpack", 32: "suitcase", 61: "chair", 62: "couch", 66: "dining table",
    76: "cell phone",
}
TRAFFIC_LIGHT = 9


@dataclass
class Detection:
    class_id: int      # raw COCO-90 index from the model
    score: float
    x: float           # normalized [0,1], top-left corner, float32 like the C++ Box
    y: float
    w: float
    h: float

    @property
    def name(self) -> str:
        return COCO_NAMES.get(self.class_id, f"class {self.class_id}")


@dataclass
class Model:
    interp: object
    backend: str
    in_w: int
    in_h: int
    input_index: int
    outputs: list  # tensor indices: boxes, classes, scores, count (C API order)


def _interpreter(path: str, threads: int):
    for mod in ("tflite_runtime.interpreter", "ai_edge_litert.interpreter"):
        try:
            m = __import__(mod, fromlist=["Interpreter"])
            return m.Interpreter(model_path=path, num_threads=threads), mod
        except ImportError:
            pass
    try:
        import tensorflow as tf
        return tf.lite.Interpreter(model_path=path, num_threads=threads), "tensorflow"
    except ImportError:
        sys.exit("no TFLite interpreter: .dev/venv/bin/pip install ai-edge-litert==1.2.0 (or tflite-runtime)")


def load_model(path: str = DEFAULT_MODEL, threads: int = 4) -> Model:
    """Loads the detector exactly as the hat does and checks the tensors are what brim expects."""
    if not os.path.exists(path):
        sys.exit(f"model not found: {path} (see models/README.md)")
    interp, backend = _interpreter(path, threads)
    interp.allocate_tensors()
    ins, outs = interp.get_input_details(), interp.get_output_details()
    inp = ins[0]
    if len(ins) != 1 or list(inp["shape"][[0, 3]]) != [1, 3] or inp["dtype"] != np.uint8:
        sys.exit(f"unexpected input {inp['shape']} {inp['dtype']}: want [1,H,W,3] uint8")
    if len(outs) != 4:
        sys.exit("expected 4 outputs: boxes, classes, scores, count")
    m = Model(interp, backend, int(inp["shape"][2]), int(inp["shape"][1]), inp["index"], [o["index"] for o in outs])
    # Warm-up, like the hat: the first real photo should not carry the slow first invoke.
    interp.set_tensor(m.input_index, np.zeros(inp["shape"], dtype=np.uint8))
    interp.invoke()
    return m


def load_image(path: str) -> np.ndarray:
    """Photo -> HxWx3 uint8 RGB, the same packed RGB888 the hat's camera delivers."""
    from PIL import Image  # pillow; imported here so the rest works without it

    with Image.open(path) as im:
        return np.asarray(im.convert("RGB"), dtype=np.uint8)


_axis_cache: dict = {}


def _axis(s: int, d: int):
    key = (s, d)
    if key not in _axis_cache:
        scale = s / d
        src = np.maximum((np.arange(d) + 0.5) * scale - 0.5, 0.0)  # pixel-centre aligned
        i0 = np.minimum(src.astype(np.int64), s - 1)
        i1 = np.where(i0 + 1 < s, i0 + 1, s - 1)
        f = np.floor((src - i0) * 256.0 + 0.5).astype(np.uint32)  # std::lround, src - i0 >= 0
        _axis_cache[key] = (i0, i1, f)
    return _axis_cache[key]


def resize_rgb(img: np.ndarray, dw: int, dh: int) -> np.ndarray:
    """Bilinear stretch with 8-bit fixed-point weights: bit-for-bit detect::RgbResizer."""
    sh, sw = img.shape[:2]
    x0, x1, fx = _axis(sw, dw)
    y0, y1, fy = _axis(sh, dh)
    src = img.astype(np.uint32)
    wx, ix = fx[None, :, None], (256 - fx)[None, :, None]
    top = src[y0][:, x0] * ix + src[y0][:, x1] * wx
    bot = src[y1][:, x0] * ix + src[y1][:, x1] * wx
    wy, iy = fy[:, None, None], (256 - fy)[:, None, None]
    return ((top * iy + bot * wy + 32768) >> 16).astype(np.uint8)


def detect(model: Model, rgb: np.ndarray) -> list[Detection]:
    """One frame through the detector. Raw rows, highest score first; boxes clamped to the frame."""
    model.interp.set_tensor(model.input_index, resize_rgb(rgb, model.in_w, model.in_h)[None, ...])
    model.interp.invoke()
    boxes, classes, scores, count = (model.interp.get_tensor(i) for i in model.outputs)
    n = int(np.clip(count.reshape(-1)[0], 0, boxes.shape[1]))
    out = []
    for i in range(n):
        score = float(scores[0, i])
        if not (0.0 <= score <= 1.0):  # NaN or garbage: never trusted (same as the hat)
            continue
        ymin, xmin, ymax, xmax = (np.float32(np.clip(v, 0, 1)) for v in boxes[0, i])
        if xmax - xmin <= 0 or ymax - ymin <= 0:
            continue
        out.append(Detection(int(round(float(classes[0, i]))), score, float(xmin), float(ymin),
                             float(np.float32(xmax - xmin)), float(np.float32(ymax - ymin))))
    out.sort(key=lambda d: -d.score)
    return out


PROBE = os.path.join(REPO, "device", "build", "host", "decide_probe")


class Decider:
    """brim's own decisions for each frame: pipes boxes to decide_probe (no Python copy of the rules)."""

    def __init__(self, probe: str = PROBE) -> None:
        if not os.path.exists(probe):
            sys.exit(f"{os.path.relpath(probe, REPO)} not found: build it with  cd device && cmake --build --preset host")
        self.proc = subprocess.Popen([probe], cwd=REPO, stdin=subprocess.PIPE, stdout=subprocess.PIPE,
                                     stderr=subprocess.DEVNULL, text=True, bufsize=1)

    def says(self, t_ms: int, dets: list[Detection], faces: dict | None = None) -> list[tuple[str, str, bool]]:
        """(prio, spoken words, named?) for this frame. `faces`: {row index: embedding}."""
        rows = " ".join(f"{d.y:.6f} {d.x:.6f} {d.y + d.h:.6f} {d.x + d.w:.6f} {d.class_id} {d.score:.6f}" for d in dets)
        embs = " ".join(f"{i} {len(e)} " + " ".join(f"{x:.6f}" for x in e) for i, e in (faces or {}).items())
        self.proc.stdin.write(f"{t_ms} {len(dets)} {rows} {len(faces or {})} {embs}\n")
        self.proc.stdin.flush()
        out = json.loads(self.proc.stdout.readline())
        return [(s["prio"], s.get("spoken", s["text"]), bool(s.get("named"))) for s in out.get("says", [])]

    def close(self) -> None:
        self.proc.stdin.close()
        self.proc.wait(timeout=5)


class MacVoice:
    """Speaks like the phone: a warning at once (cuts off anything else), a direction after it, and a
    direction that is word for word the warning just said is not said again."""

    def __init__(self, enabled: bool) -> None:
        self.enabled = enabled and shutil.which("say") is not None
        if enabled and not self.enabled:
            print("note: no 'say' command (macOS only); printing SAYS lines only")
        self.proc = None
        self.pending: str | None = None
        self.last_alert = ("", -1e9)

    def busy(self) -> bool:
        return self.proc is not None and self.proc.poll() is None

    def push(self, prio: str, text: str, now: float) -> None:
        if prio == "safety":
            self.last_alert = (text, now)
            self.pending = None
            if self.enabled:
                if self.busy():
                    self.proc.terminate()
                self.proc = subprocess.Popen(["say", text])
        elif not (text == self.last_alert[0] and now - self.last_alert[1] <= 5.0):
            self.pending = text  # latest direction wins
        self.pump()

    def pump(self) -> None:
        if not self.pending:
            return
        if not self.enabled:
            self.pending = None
        elif not self.busy():
            self.proc = subprocess.Popen(["say", self.pending])
            self.pending = None

    def finish(self) -> None:
        if self.busy():
            self.proc.wait(timeout=10)


def live(argv: list[str]) -> int:
    ap = argparse.ArgumentParser(prog="brim_test.py live", description="The hat's detector and words, live on the webcam.")
    ap.add_argument("--camera", default="0", help="camera number, or a video file to replay")
    ap.add_argument("--model", default=DEFAULT_MODEL)
    ap.add_argument("--threads", type=int, default=4)
    ap.add_argument("--say", action="store_true", help="speak the SAYS lines on the Mac")
    ap.add_argument("--no-window", action="store_true", help="no preview window (replaying a video file)")
    ap.add_argument("--why", action="store_true",
                    help="once a second, print each step: person found? close? face found? match score? (numbers only)")
    a = ap.parse_args(argv)
    try:
        import cv2
    except ImportError:
        sys.exit('needs OpenCV: .dev/venv/bin/pip install "opencv-python==4.12.0.88"')
    model = load_model(a.model, a.threads)
    decider = Decider()
    # Faces: only if somebody is enrolled (same rule as the hat). Otherwise no face model is loaded.
    sys.path.insert(0, os.path.join(REPO, "scripts", "faces"))
    from common import embed_in_box, enrolled, load_config as load_face_config, models as face_models
    fcfg = load_face_config()
    people = enrolled(fcfg)
    face_det = face_rec = None
    if people:
        _, face_det, face_rec = face_models(fcfg)
        print(f"faces: {len(people)} enrolled ({', '.join(people)}); strangers stay 'Person'")
    else:
        print("faces: nobody enrolled, no face model runs")
    voice = MacVoice(a.say)
    src = int(a.camera) if a.camera.isdigit() else a.camera
    cap = cv2.VideoCapture(src)
    if not cap.isOpened():
        sys.exit(f"camera {a.camera} did not open. On a Mac, allow camera access for your terminal "
                 "(System Settings > Privacy & Security > Camera) and run again.")
    if isinstance(src, int):  # the hat's frame size
        cap.set(cv2.CAP_PROP_FRAME_WIDTH, 640)
        cap.set(cv2.CAP_PROP_FRAME_HEIGHT, 480)
    print(f"camera {a.camera} open; q in the window quits. Nothing is saved.")
    t0 = time.perf_counter()
    last_said = ""
    last_why = -1.0
    if a.why and face_rec is not None:  # enrolled embeddings, for the --why printout only (C++ decides)
        import numpy as np
        refs = {}
        for name in people:
            with open(os.path.join(fcfg["faces_dir"] if os.path.isabs(fcfg["faces_dir"]) else os.path.join(REPO, fcfg["faces_dir"]),
                                   name, "embeddings.json")) as f:
                refs[name] = [np.asarray(e, dtype=np.float32) / np.linalg.norm(e) for e in json.load(f)["embeddings"]]
    try:
        while True:
            ok, bgr = cap.read()
            if not ok:
                break
            rgb = cv2.cvtColor(bgr, cv2.COLOR_BGR2RGB)
            dets = detect(model, rgb)
            now = time.perf_counter() - t0
            faces = {}
            if face_det is not None:  # person boxes only; the embeddings live for this frame
                for i, d in enumerate(dets):
                    if d.class_id == 0 and d.score >= 0.6:
                        e = embed_in_box(fcfg, face_det, face_rec, bgr, (d.x, d.y, d.w, d.h))
                        if e is not None:
                            faces[i] = e
            if a.why and now - last_why >= 1.0:
                last_why = now
                persons = [d for d in dets if d.class_id == 0]
                if not persons:
                    print(f"{now:7.1f} s  why: no person box at all")
                else:
                    p = max(persons, key=lambda d: d.score)
                    i = dets.index(p)
                    msg = (f"{now:7.1f} s  why: person score {p.score:.2f} (need 0.60), fills {p.w * p.h:.0%} of the frame "
                           f"(close >= 18%, centre x {p.x + p.w / 2:.2f})")
                    if face_det is None:
                        msg += " | nobody enrolled"
                    elif p.score < 0.6:
                        msg += " | face not checked (person score too low)"
                    elif i not in faces:
                        msg += " | NO FACE found in the box (too small, turned away, dark, or face score < "
                        msg += f"{fcfg['face_min_score']})"
                    else:
                        e = np.asarray(faces[i], dtype=np.float32)
                        e = e / np.linalg.norm(e)
                        best = max(((n, max(float(r @ e) for r in rs)) for n, rs in refs.items()), key=lambda t: t[1])
                        msg += f" | face found, best match {best[0]} {best[1]:.2f} (need {fcfg['match_threshold']})"
                    print(msg)
            for prio, text, named in decider.says(int(now * 1000), dets, faces):
                print(f"{now:7.1f} s  SAYS [{prio}] {text}" + ("   (enrolled person)" if named else ""))
                voice.push(prio, text, now)
                last_said = text
            faces.clear()
            voice.pump()
            if a.no_window:
                continue
            h, w = bgr.shape[:2]
            for d in dets:
                if d.class_id in COCO_NAMES and d.score >= 0.5:
                    p0, p1 = (int(d.x * w), int(d.y * h)), (int((d.x + d.w) * w), int((d.y + d.h) * h))
                    cv2.rectangle(bgr, p0, p1, (230, 230, 230), 2)
                    cv2.putText(bgr, f"{d.name} {d.score:.2f}", (p0[0] + 2, max(16, p0[1] - 6)),
                                cv2.FONT_HERSHEY_SIMPLEX, 0.55, (230, 230, 230), 2)
            cv2.rectangle(bgr, (0, 0), (w, 34), (20, 20, 20), -1)
            cv2.putText(bgr, f"SAYS: {last_said or '-'}", (10, 23), cv2.FONT_HERSHEY_SIMPLEX, 0.65, (255, 255, 255), 2)
            cv2.imshow("Brim live (nothing is saved)", bgr)
            if cv2.waitKey(1) & 0xFF in (ord("q"), 27):
                break
    except KeyboardInterrupt:
        pass
    finally:
        cap.release()
        cv2.destroyAllWindows()
        decider.close()
        voice.finish()
    return 0


def main() -> None:
    if len(sys.argv) > 1 and sys.argv[1] == "live":
        sys.exit(live(sys.argv[2:]))
    ap = argparse.ArgumentParser(description=__doc__.split("\n\n")[0])
    ap.add_argument("photos", nargs="+")
    ap.add_argument("--model", default=DEFAULT_MODEL)
    ap.add_argument("--threads", type=int, default=4)
    ap.add_argument("--min-score", type=float, default=0.3, help="only print rows at or above this score")
    a = ap.parse_args()
    model = load_model(a.model, a.threads)
    print(f"model {os.path.relpath(a.model, REPO)}, input {model.in_w}x{model.in_h}, backend {model.backend}")
    for p in a.photos:
        rgb = load_image(p)
        t0 = time.perf_counter()
        dets = detect(model, rgb)
        ms = (time.perf_counter() - t0) * 1000
        print(f"{os.path.basename(p)}  {rgb.shape[1]}x{rgb.shape[0]}  {ms:.1f} ms")
        for d in dets:
            if d.score >= a.min_score:
                print(f"    {d.name:<14} {d.score:.2f}  x{d.x:.2f} y{d.y:.2f} w{d.w:.2f} h{d.h:.2f}")


if __name__ == "__main__":
    main()
