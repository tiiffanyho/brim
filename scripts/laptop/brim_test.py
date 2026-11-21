#!/usr/bin/env python3
"""Brim's person/object detector on the laptop, on photos.

Same model, same input and same outputs as the hat (device/src/detect/tflite_detector.cpp):
EfficientDet-Lite0 uint8 (models/efficientdet_lite0.tflite), RGB frame stretched to 320x320 with
the same bilinear resize as detect::RgbResizer, outputs boxes / classes / scores / count from the
in-graph TFLite_Detection_PostProcess op. Laptop only: nothing here runs on the hat.

    python3 -m venv .dev/venv
    .dev/venv/bin/pip install "ai-edge-litert==1.2.0" numpy pillow
    .dev/venv/bin/python scripts/laptop/brim_test.py data/traffic_test/IMG_0001.jpg [...]

As a library (scripts/test_traffic.py):  model = load_model();  dets = detect(model, load_image(p))
detect() returns RAW model rows (COCO-90 class index, score, box): no Brim class mapping and no
threshold, so a test can read any class directly (e.g. 9 = traffic light).

Privacy: photos are only read. Nothing is written, uploaded or kept, and no face model exists.
"""
from __future__ import annotations

import argparse
import os
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


def main() -> None:
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
