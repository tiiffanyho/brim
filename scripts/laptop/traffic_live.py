#!/usr/bin/env python3
"""Live traffic light test: open the C270, show it a traffic light, see what the hat would say.

Runs the hat's detector (brim_test.py) and colour check (test_traffic.py, config/traffic.json) on
every camera frame. The window shows the box, score, colour and phrase; the terminal prints each
cue the hat would send (one per 10 s, like brim). Laptop only. Feasibility test, not a crossing
aid: the only phrases are 'Traffic light ahead, red|green|colour unknown'.

    .dev/venv/bin/pip install "ai-edge-litert==1.2.0" numpy pillow "opencv-python==4.12.0.88"
    .dev/venv/bin/python scripts/laptop/traffic_live.py            # camera 0
    .dev/venv/bin/python scripts/laptop/traffic_live.py --list     # which camera number is the C270?
    .dev/venv/bin/python scripts/laptop/traffic_live.py --camera 1 --say

Keys (in the window):  r red   g green   a amber   o light off   n no light   space not labelling
                       c reload config/traffic.json   q or Esc quit and print the summary
Press a key for what is really in front of the camera; frames are scored against it until you
press another. Unlabelled frames are shown but not scored.

Privacy: frames live in memory only. Nothing is saved, recorded or uploaded; only counts are kept.
"""
from __future__ import annotations

import argparse
import os
import subprocess
import sys
import time

import numpy as np

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
sys.path.insert(0, os.path.dirname(HERE))
from brim_test import DEFAULT_MODEL, TRAFFIC_LIGHT, detect, load_model  # noqa: E402
from test_traffic import (MAX_FALSE_DETECTIONS, MAX_MS_PER_FRAME, MIN_COLOUR_ACCURACY,  # noqa: E402
                          MIN_DETECTION_RATE, PHRASES, REPO, classify_light, load_config)

try:
    import cv2
except ImportError:
    sys.exit('needs OpenCV: .dev/venv/bin/pip install "opencv-python==4.12.0.88"')

KEYS = {ord("r"): "red", ord("g"): "green", ord("a"): "amber", ord("o"): "off", ord("n"): "none", ord(" "): None}
EXPECTED = {"red": "red", "green": "green", "amber": "unknown", "off": "unknown"}
CUE_INTERVAL_S = 10.0
COLOURS_BGR = {"red": (40, 40, 230), "green": (60, 200, 60), "unknown": (0, 200, 230), "-": (160, 160, 160)}


def list_cameras() -> None:
    print("Trying camera numbers 0-5 (the C270 is usually 1 on a Mac with a built-in camera):")
    found = 0
    for i in range(6):
        cap = cv2.VideoCapture(i)
        if cap.isOpened():
            found += 1
            ok, frame = cap.read()
            print(f"  {i}: {'opens' if ok else 'opens, no frame'}" + (f", {frame.shape[1]}x{frame.shape[0]}" if ok else ""))
        cap.release()
    if not found:
        print("  none opened. On a Mac, allow camera access for your terminal app (System Settings > Privacy &\n"
              "  Security > Camera), quit and reopen the terminal, then run --list again.")


class Stats:
    """Per-label frame counts. Numbers only: no frame, box or image is kept."""

    def __init__(self) -> None:
        self.frames = {k: 0 for k in EXPECTED.keys() | {"none"}}
        self.detected = dict.fromkeys(self.frames, 0)
        self.called = 0       # detected frames where the colour was red or green
        self.called_ok = 0
        self.flips = 0        # colour changes between consecutive detected frames
        self.detected_run_s = 0.0
        self.ms: list[float] = []
        self.cues: list[tuple[float, str, str | None]] = []  # (t, phrase, label then)

    def add(self, label: str | None, detected: bool, colour: str, ms: float) -> None:
        self.ms.append(ms)
        if label is None:
            return
        self.frames[label] += 1
        if detected:
            self.detected[label] += 1
            if colour in ("red", "green"):
                self.called += 1
                self.called_ok += colour == label


def draw(frame, best, detected, colour, info, label, ms, fps, last_cue, min_score) -> None:
    h, w = frame.shape[:2]
    if best is not None:
        x0, y0 = int(best.x * w), int(best.y * h)
        x1, y1 = int((best.x + best.w) * w), int((best.y + best.h) * h)
        col = COLOURS_BGR[colour] if detected else COLOURS_BGR["-"]
        cv2.rectangle(frame, (x0, y0), (x1, y1), col, 2)
        tag = f"{best.score:.2f}" + (f" {colour}" if detected else f" < {min_score}")
        cv2.putText(frame, tag, (x0, max(18, y0 - 6)), cv2.FONT_HERSHEY_SIMPLEX, 0.6, col, 2)
    cv2.rectangle(frame, (0, 0), (w, 64), (20, 20, 20), -1)
    phrase = PHRASES[colour] if detected else "no traffic light"
    cv2.putText(frame, phrase, (10, 24), cv2.FONT_HERSHEY_SIMPLEX, 0.7,
                COLOURS_BGR[colour] if detected else (200, 200, 200), 2)
    px = f"red {info['red']} green {info['green']} of {info['total']} px" if info else ""
    cv2.putText(frame, f"{ms:.1f} ms  {fps:.0f} fps  {px}", (10, 48), cv2.FONT_HERSHEY_SIMPLEX, 0.5, (200, 200, 200), 1)
    lab = f"truth: {label}" if label else "truth: not labelling (r g a o n)"
    cv2.putText(frame, lab, (10, h - 12), cv2.FONT_HERSHEY_SIMPLEX, 0.6, (255, 255, 255), 2)
    if last_cue:
        cv2.putText(frame, f"last cue: {last_cue}", (10, h - 40), cv2.FONT_HERSHEY_SIMPLEX, 0.5, (255, 255, 255), 1)


def summary(st: Stats, elapsed: float) -> int:
    pct = lambda n, d: f"{100.0 * n / d:.1f}%" if d else "n/a"  # noqa: E731
    lit = [k for k in EXPECTED]
    light_frames = sum(st.frames[k] for k in lit)
    light_found = sum(st.detected[k] for k in lit)
    none_frames, none_found = st.frames["none"], st.detected["none"]
    avg_ms = sum(st.ms) / len(st.ms) if st.ms else float("nan")
    print()
    print(f"Session: {elapsed:.0f} s, {len(st.ms)} frames, {light_frames + none_frames} labelled")
    for k in ("red", "green", "amber", "off", "none"):
        if st.frames[k]:
            print(f"  {k:<6} {st.frames[k]:5d} frames, traffic light found in {pct(st.detected[k], st.frames[k])}")
    print(f"  colour flips while a light was in view: {st.flips}"
          + (f" ({st.flips / (st.detected_run_s / 60):.1f} per minute)" if st.detected_run_s > 5 else ""))
    wrong = [c for c in st.cues if c[2] in EXPECTED and c[1] not in (PHRASES[EXPECTED[c[2]]], PHRASES["unknown"])]
    wrong += [c for c in st.cues if c[2] == "none"]
    print(f"  cues the hat would have sent: {len(st.cues)}, wrong: {len(wrong)}")
    for t, phrase, lab in wrong:
        print(f"    {t:6.1f} s  said '{phrase}' while truth was {lab}")
    print()
    print("Pass criteria, per frame (photos are the official record: scripts/test_traffic.py)")
    v = lambda ok: "PASS" if ok else "FAIL"  # noqa: E731
    c1 = light_frames > 0 and light_found / light_frames >= MIN_DETECTION_RATE
    c2 = none_frames > 0 and none_found / none_frames <= MAX_FALSE_DETECTIONS / 30
    c3 = st.called > 0 and st.called_ok / st.called >= MIN_COLOUR_ACCURACY
    c4 = bool(st.ms) and avg_ms < MAX_MS_PER_FRAME
    print(f"  {v(c1)}  detection with a light      {light_found}/{light_frames} = {pct(light_found, light_frames)} (need >= 90%)")
    print(f"  {v(c2)}  false detection, no light   {none_found}/{none_frames} = {pct(none_found, none_frames)} (need <= 3.3%, i.e. 1 in 30)")
    print(f"  {v(c3)}  colour correct (red/green)  {st.called_ok}/{st.called} = {pct(st.called_ok, st.called)} (need >= 85%)")
    print(f"  {v(c4)}  detector + colour           {avg_ms:.1f} ms avg on this laptop (the Pi decides: need < 30 ms there)")
    if not light_frames and not none_frames:
        print("  (nothing labelled: press r, g, a, o or n while testing to get scores)")
    return 0 if (c1 and c2 and c3 and c4) else 1


def main() -> int:
    ap = argparse.ArgumentParser(description="Live traffic light test on the webcam.")
    ap.add_argument("--camera", default="0", help="camera number (see --list), or a video file to replay")
    ap.add_argument("--list", action="store_true", help="show which camera numbers open, then exit")
    ap.add_argument("--config", default=os.path.join(REPO, "config", "traffic.json"))
    ap.add_argument("--model", default=DEFAULT_MODEL)
    ap.add_argument("--threads", type=int, default=4)
    ap.add_argument("--say", action="store_true", help="speak each cue with macOS 'say' (the hat itself plays no sound)")
    ap.add_argument("--no-window", action="store_true", help="no preview window (replaying a video file in a script)")
    a = ap.parse_args()

    if a.list:
        list_cameras()
        return 0
    cfg = load_config(a.config)
    model = load_model(a.model, a.threads)
    src = int(a.camera) if a.camera.isdigit() else a.camera
    cap = cv2.VideoCapture(src)
    if not cap.isOpened():
        sys.exit(f"camera {a.camera} did not open. Try --list. On a Mac, allow camera access for your terminal "
                 "(System Settings > Privacy & Security > Camera) and run again.")
    if isinstance(src, int):  # the hat's frame size
        cap.set(cv2.CAP_PROP_FRAME_WIDTH, 640)
        cap.set(cv2.CAP_PROP_FRAME_HEIGHT, 480)
    print(f"camera {a.camera} open. In the window: r g a o n = what is really there, space = not labelling, "
          "c = reload config, q = quit")

    st = Stats()
    label: str | None = None
    last_cue_t = -1e9
    last_cue = ""
    prev_colour = None
    t_start = t_prev = time.perf_counter()
    fps = 0.0
    try:
        while True:
            ok, bgr = cap.read()
            if not ok:
                break
            rgb = cv2.cvtColor(bgr, cv2.COLOR_BGR2RGB)
            t0 = time.perf_counter()
            lights = [d for d in detect(model, rgb) if d.class_id == TRAFFIC_LIGHT]
            best = lights[0] if lights else None
            detected = best is not None and best.score >= cfg["min_score"]
            info = classify_light(rgb, best, cfg) if detected else None
            colour = info["colour"] if info else "-"
            ms = (time.perf_counter() - t0) * 1000.0
            now = time.perf_counter()
            fps = 0.9 * fps + 0.1 * (1.0 / max(now - t_prev, 1e-6)) if fps else 1.0 / max(now - t_prev, 1e-6)
            if detected:
                if prev_colour is not None and colour != prev_colour:
                    st.flips += 1
                st.detected_run_s += now - t_prev
            prev_colour = colour if detected else None
            t_prev = now
            st.add(label, detected, colour, ms)

            # One cue per 10 s while a light is in view, as brim does (colour from the cue frame).
            if detected and now - last_cue_t >= CUE_INTERVAL_S:
                last_cue_t = now
                last_cue = PHRASES[colour]
                st.cues.append((now - t_start, last_cue, label))
                print(f"{now - t_start:7.1f} s  CUE  {last_cue}   (score {best.score:.2f}, red {info['red']}, "
                      f"green {info['green']} of {info['total']} px, truth {label or '-'})")
                if a.say:
                    subprocess.Popen(["say", last_cue])

            if a.no_window:
                continue
            draw(bgr, best, detected, colour, info, label, ms, fps, last_cue, cfg["min_score"])
            cv2.imshow("Brim traffic light test (nothing is saved)", bgr)
            k = cv2.waitKey(1) & 0xFF
            if k in (ord("q"), 27):
                break
            if k in KEYS:
                label = KEYS[k]
                print(f"{now - t_start:7.1f} s  truth: {label or 'not labelling'}")
            elif k == ord("c"):
                cfg = load_config(a.config)
                print(f"{now - t_start:7.1f} s  reloaded {os.path.relpath(a.config, REPO)}")
    except KeyboardInterrupt:
        pass
    finally:
        cap.release()
        cv2.destroyAllWindows()
    return summary(st, time.perf_counter() - t_start)


if __name__ == "__main__":
    sys.exit(main())
