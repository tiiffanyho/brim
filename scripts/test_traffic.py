#!/usr/bin/env python3
"""Traffic light feasibility test on labelled photos (laptop first, then the Pi).

Can the detector find a traffic light (raw COCO class 9) and read red or green from its colour?
A lab test with a light or screen at hat height. NOT a crossing feature: Brim never says walk,
cross, go or safe to cross; it only reports 'Traffic light ahead, red|green|colour unknown'.

    .dev/venv/bin/pip install "ai-edge-litert==1.2.0" numpy pillow     (same venv as brim_test.py)
    .dev/venv/bin/python scripts/test_traffic.py                       (data/traffic_test/)
    .dev/venv/bin/python scripts/test_traffic.py --self-test           (colour check only, no photos)

data/traffic_test/ (git-ignored) holds the C270 photos and labels.csv:

    photo,label,notes
    IMG_0001.jpg,red,1.5 m, hat tilt as in the demo walkway
    IMG_0031.jpg,none,

label is red, green, off (a light that is not lit) or none (no traffic light in the photo); extra
columns are ignored. Only photos of teammates who agreed; they stay on this laptop and are only
read here: nothing is written, uploaded or kept.

The detector is scripts/laptop/brim_test.py (same model, input and outputs as the hat). The colour
check below mirrors device/src/detect/traffic_light.cpp exactly and reads config/traffic.json.
Exit code 0 only when every pass criterion passes.
"""
from __future__ import annotations

import argparse
import csv
import json
import math
import os
import sys
import time

import numpy as np

HERE = os.path.dirname(os.path.abspath(__file__))
REPO = os.path.dirname(HERE)
sys.path.insert(0, os.path.join(HERE, "laptop"))
from brim_test import DEFAULT_MODEL, TRAFFIC_LIGHT, Detection, detect, load_image, load_model  # noqa: E402

LABELS = ("red", "green", "off", "none")
EXPECTED_COLOUR = {"red": "red", "green": "green", "off": "unknown"}
PHRASES = {  # the only things Brim may say about a traffic light (device/src/detect/traffic_light.hpp)
    "red": "Traffic light ahead, red",
    "green": "Traffic light ahead, green",
    "unknown": "Traffic light ahead, colour unknown",
}

# Pass criteria (docs: "Traffic light test").
MIN_DETECTION_RATE = 0.90
MAX_FALSE_DETECTIONS = 1  # of the 30 photos without a light
MIN_COLOUR_ACCURACY = 0.85
MAX_MS_PER_FRAME = 30.0  # on the Pi; otherwise run every 2nd frame


# ------------------------------------------------------------------ config/traffic.json
def load_config(path: str) -> dict:
    with open(path) as f:
        c = json.load(f)
    for k in ("min_score", "min_bright", "min_saturation", "min_share", "min_dominance", "red_hue", "green_hue"):
        if k not in c:
            sys.exit(f"{path}: missing {k}")
    for k in ("red_hue", "green_hue"):
        for lo, hi in c[k]:
            if not (0 <= lo <= hi <= 179):
                sys.exit(f"{path}: {k} range [{lo}, {hi}] is not inside 0..179")
    return c


# ------------------------------------------------------------------ colour check (mirrors traffic_light.cpp)
def rgb_to_hsv(rgb: np.ndarray):
    """OpenCV-convention HSV of uint8 RGB pixels: h 0-179, s and v 0-255 (int arrays)."""
    r, g, b = (rgb[..., i].astype(np.int64) for i in range(3))
    mx = np.maximum(np.maximum(r, g), b)
    mn = np.minimum(np.minimum(r, g), b)
    d = mx - mn
    v = mx
    s = np.where(mx == 0, 0, (255 * d + mx // 2) // np.maximum(mx, 1))
    dd = np.maximum(d, 1).astype(np.float64)
    deg = np.where(mx == r, 60.0 * (g - b) / dd, np.where(mx == g, 120.0 + 60.0 * (b - r) / dd, 240.0 + 60.0 * (r - g) / dd))
    deg = np.where(deg < 0, deg + 360.0, deg)
    h = np.floor(deg / 2.0 + 0.5).astype(np.int64) % 180
    h = np.where(d == 0, 0, h)
    return h, s, v


def _edge(frac: float, size: int) -> int:
    return min(max(int(math.floor(frac * size + 0.5)), 0), size)


def _in_ranges(h: np.ndarray, ranges) -> np.ndarray:
    m = np.zeros(h.shape, dtype=bool)
    for lo, hi in ranges:
        m |= (h >= lo) & (h <= hi)
    return m


def classify_light(rgb: np.ndarray, box: Detection, cfg: dict) -> dict:
    """Bright red-hue vs green-hue pixels inside the box -> red, green or unknown (+ the counts)."""
    out = {"colour": "unknown", "red": 0, "green": 0, "total": 0, "share": 0.0}
    hgt, wid = rgb.shape[:2]
    x0, y0 = _edge(box.x, wid), _edge(box.y, hgt)
    x1, y1 = _edge(box.x + box.w, wid), _edge(box.y + box.h, hgt)
    if x1 <= x0 or y1 <= y0:
        return out
    h, s, v = rgb_to_hsv(rgb[y0:y1, x0:x1])
    lit = (v >= cfg["min_bright"]) & (s >= cfg["min_saturation"])
    red = lit & _in_ranges(h, cfg["red_hue"])
    green = lit & ~red & _in_ranges(h, cfg["green_hue"])
    out["red"], out["green"], out["total"] = int(red.sum()), int(green.sum()), (x1 - x0) * (y1 - y0)
    red_wins = out["red"] > out["green"]
    win, lose = (out["red"], out["green"]) if red_wins else (out["green"], out["red"])
    if win == lose:
        return out  # tie, including nothing lit: not sure
    share = win / out["total"]
    if share < cfg["min_share"] or win < cfg["min_dominance"] * lose:
        return out
    out["colour"] = "red" if red_wins else "green"
    out["share"] = share
    return out


# ------------------------------------------------------------------ self-test: the C++ unit cases
def self_test(cfg: dict) -> bool:
    """Same synthetic crops as device/tests/test_traffic.cpp: proves the Python mirror agrees."""

    def img(*rects):
        a = np.full((100, 100, 3), 40, dtype=np.uint8)
        for x0, y0, x1, y1, rgb in rects:
            a[y0:y1, x0:x1] = rgb
        return a

    box = Detection(TRAFFIC_LIGHT, 0.9, float(np.float32(0.40)), float(np.float32(0.10)), float(np.float32(0.20)),
                    float(np.float32(0.60)))
    red_lamp, green_lamp = (255, 30, 20), (0, 230, 170)
    cases = [
        ("red lamp", img((44, 14, 56, 30, red_lamp)), "red"),
        ("green lamp", img((44, 50, 56, 66, green_lamp)), "green"),
        ("off", img((44, 14, 56, 30, (90, 10, 10)), (44, 50, 56, 66, (10, 90, 60))), "unknown"),
        ("amber", img((44, 32, 56, 48, (255, 170, 0))), "unknown"),
        ("glare", img((44, 14, 56, 30, (255, 250, 245))), "unknown"),
        ("too small", img((48, 20, 52, 24, red_lamp)), "unknown"),
        ("mixed 1.5x", img((44, 14, 56, 30, red_lamp), (44, 50, 52, 66, green_lamp)), "unknown"),
        ("tie", img((44, 14, 56, 30, red_lamp), (44, 50, 56, 66, green_lamp)), "unknown"),
        ("red wall outside box", img((0, 0, 30, 100, red_lamp)), "unknown"),
    ]
    hsv_cases = [((255, 0, 0), (0, 255, 255)), ((0, 255, 0), (60, 255, 255)), ((0, 0, 255), (120, 255, 255)),
                 ((0, 255, 180), (81, 255, 255)), ((255, 170, 0), (20, 255, 255)), ((255, 0, 40), (175, 255, 255)),
                 ((250, 250, 250), (0, 0, 250)), ((0, 0, 0), (0, 0, 0))]
    ok = True
    for rgb, want in hsv_cases:
        h, s, v = rgb_to_hsv(np.array([[rgb]], dtype=np.uint8))
        got = (int(h[0, 0]), int(s[0, 0]), int(v[0, 0]))
        if got != want:
            print(f"  FAIL hsv{rgb}: got {got}, want {want}")
            ok = False
    for name, a, want in cases:
        r = classify_light(a, box, cfg)
        good = r["colour"] == want
        ok &= good
        print(f"  {'ok  ' if good else 'FAIL'} {name:<22} -> {r['colour']:<8} (red {r['red']}, green {r['green']} of {r['total']} px)")
    first = classify_light(cases[0][1], box, cfg)
    if first["total"] != 1200 or first["red"] != 192:
        print(f"  FAIL box bounds: {first['total']} px / {first['red']} red, want 1200 / 192 (same as the C++ test)")
        ok = False
    for p in PHRASES.values():
        words = set("".join(ch if ch.isalpha() else " " for ch in p.lower()).split())
        if words & {"walk", "cross", "crossing", "go", "safe", "left", "right", "step", "stop"}:
            print(f"  FAIL phrase {p!r} says more than what was seen")
            ok = False
    print("self-test:", "PASS" if ok else "FAIL")
    return ok


# ------------------------------------------------------------------ the photo test
def read_labels(path: str) -> list[tuple[str, str]]:
    if not os.path.exists(path):
        sys.exit(f"no labels file: {path}\n(columns: photo,label with label one of {', '.join(LABELS)})")
    rows = []
    with open(path, newline="") as f:
        r = csv.DictReader(f)
        if not r.fieldnames or "photo" not in r.fieldnames or "label" not in r.fieldnames:
            sys.exit(f"{path}: the first line must be a header with 'photo' and 'label'")
        for i, row in enumerate(r, start=2):
            photo, label = (row.get("photo") or "").strip(), (row.get("label") or "").strip().lower()
            if not photo:
                continue
            if label not in LABELS:
                sys.exit(f"{path}:{i}: label {label!r} is not one of {', '.join(LABELS)}")
            rows.append((photo, label))
    return rows


def pct(n: int, d: int) -> str:
    return f"{100.0 * n / d:.1f}%" if d else "n/a"


def verdict(ok: bool) -> str:
    return "PASS" if ok else "FAIL"


def main() -> int:
    ap = argparse.ArgumentParser(description="Traffic light feasibility test on labelled photos.")
    ap.add_argument("--dir", default=os.path.join(REPO, "data", "traffic_test"))
    ap.add_argument("--labels", help="default: <dir>/labels.csv")
    ap.add_argument("--config", default=os.path.join(REPO, "config", "traffic.json"))
    ap.add_argument("--model", default=DEFAULT_MODEL)
    ap.add_argument("--threads", type=int, default=4)
    ap.add_argument("--self-test", action="store_true", help="check the colour check against the C++ unit cases and exit")
    a = ap.parse_args()

    cfg = load_config(a.config)
    if a.self_test:
        return 0 if self_test(cfg) else 1

    rows = read_labels(a.labels or os.path.join(a.dir, "labels.csv"))
    if not rows:
        sys.exit("labels.csv has no photos")
    model = load_model(a.model, a.threads)
    print(f"model {os.path.relpath(a.model, REPO)} ({model.backend}, {a.threads} threads), "
          f"config {os.path.relpath(a.config, REPO)}: min_score {cfg['min_score']}, min_share {cfg['min_share']}, "
          f"min_dominance {cfg['min_dominance']}, min_bright {cfg['min_bright']}, min_saturation {cfg['min_saturation']}")
    print()
    hdr = f"{'photo':<28} {'label':<6} {'detected':<8} {'score':>5}  {'colour':<8} {'correct':<7} {'ms':>6}"
    print(hdr)
    print("-" * len(hdr))

    results, missing = [], []
    for photo, label in rows:
        path = os.path.join(a.dir, photo)
        if not os.path.exists(path):
            missing.append(photo)
            print(f"{photo:<28} {label:<6} MISSING")
            continue
        rgb = load_image(path)  # not timed: the hat's camera hands over RGB directly
        t0 = time.perf_counter()
        lights = [d for d in detect(model, rgb) if d.class_id == TRAFFIC_LIGHT]
        best = lights[0] if lights else None  # highest score first
        detected = best is not None and best.score >= cfg["min_score"]
        colour = classify_light(rgb, best, cfg)["colour"] if detected else "-"
        ms = (time.perf_counter() - t0) * 1000.0
        if label == "none":
            correct = not detected
        else:
            correct = detected and colour == EXPECTED_COLOUR[label]
        results.append({"photo": photo, "label": label, "detected": detected, "colour": colour, "correct": correct, "ms": ms})
        score = f"{best.score:.2f}" if best else "-"  # best raw class-9 score, even under min_score
        print(f"{photo:<28} {label:<6} {'yes' if detected else 'no':<8} {score:>5}  {colour:<8} "
              f"{'yes' if correct else 'no':<7} {ms:6.1f}")

    with_light = [r for r in results if r["label"] != "none"]
    without = [r for r in results if r["label"] == "none"]
    found = sum(r["detected"] for r in with_light)
    false_det = sum(r["detected"] for r in without)
    coloured = [r for r in results if r["detected"] and r["colour"] in ("red", "green")]
    colour_ok = sum(r["colour"] == r["label"] for r in coloured)
    avg_ms = sum(r["ms"] for r in results) / len(results) if results else float("nan")

    det_rate = found / len(with_light) if with_light else 0.0
    col_acc = colour_ok / len(coloured) if coloured else 0.0
    c1 = bool(with_light) and det_rate >= MIN_DETECTION_RATE
    c2 = bool(without) and false_det <= MAX_FALSE_DETECTIONS
    c3 = bool(coloured) and col_acc >= MIN_COLOUR_ACCURACY
    c4 = bool(results) and avg_ms < MAX_MS_PER_FRAME

    print()
    print("Summary")
    print(f"  photos: {len(results)} read ({len(with_light)} with a light, {len(without)} without)"
          + (f", {len(missing)} MISSING: {', '.join(missing)}" if missing else ""))
    if len(with_light) != 30 or len(without) != 30:
        print("  note: the test plan asks for 30 photos with a light and 30 without")
    unknown = sum(r["detected"] and r["colour"] == "unknown" for r in results)
    print(f"  detections with colour unknown: {unknown} (said as 'colour unknown', never a guess)")
    print()
    print(f"  {verdict(c1)}  detection on photos with a light   {found}/{len(with_light)} = {pct(found, len(with_light))}"
          f"   (need >= {MIN_DETECTION_RATE:.0%})")
    print(f"  {verdict(c2)}  false detections without a light   {false_det}/{len(without)}"
          f"   (need <= {MAX_FALSE_DETECTIONS} of 30)")
    print(f"  {verdict(c3)}  colour correct (red/green calls)   {colour_ok}/{len(coloured)} = {pct(colour_ok, len(coloured))}"
          f"   (need >= {MIN_COLOUR_ACCURACY:.0%})")
    print(f"  {verdict(c4)}  detector + colour, avg per frame   {avg_ms:.1f} ms on this laptop"
          f"   (need < {MAX_MS_PER_FRAME:.0f} ms ON THE PI; laptop numbers do not decide this one)")
    failed = [r for r in results if not r["correct"]]
    if failed:
        print()
        print("  failed photos (report these with the camera angle):")
        for r in failed:
            why = ("false detection" if r["label"] == "none" else "not detected" if not r["detected"]
                   else f"said {r['colour']}, was {r['label']}")
            print(f"    {r['photo']:<28} {why}")
    ok = c1 and c2 and c3 and c4 and not missing
    print()
    print(f"OVERALL: {verdict(ok)}" + ("" if ok else "  -> traffic lights stay out of the demo (future work)"))
    return 0 if ok else 1


if __name__ == "__main__":
    sys.exit(main())
