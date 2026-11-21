#!/usr/bin/env python3
"""Live object accuracy test: open the C270, point it at things, press a key for what is there.

The live version of object_test.py: the hat's detector (brim_test.py) and Brim's own mapping and
thresholds (read from device/src/detect and config/safety.json, never changed) on every camera
frame. The window draws every box the hat would act on, with its Brim name. Test only the classes
you have to hand: classes you never label are reported as "not tested", not as failures. Nothing
is spoken and no phrase is added.

    .dev/venv/bin/pip install "ai-edge-litert==1.2.0" numpy pillow "opencv-python==4.12.0.88"
    .dev/venv/bin/python scripts/laptop/object_live.py --list        # which camera is the C270?
    .dev/venv/bin/python scripts/laptop/object_live.py --camera 0 --say   # speaks, for testing eyes-closed

Keys (click the window first): press the key for what is really in front of the camera; frames are
scored against it until you press another.
    p person    b bicycle   c car       m motorcycle  u bus     t truck    h chair
    d table     f phone     e bench     k backpack    s suitcase  o couch
    n none (nothing from the list in view, no people)   space not labelling   q / Esc quit + summary

Per class: detected on >= 85% of its frames; false alarms <= 1 in 20 "none" frames; a cell phone is
never an obstacle. Frames next to each other look alike, so move: change distance, angle and light.
Vehicles parked or on private property only, never on a public road or near traffic.

--say speaks on the Mac what the hat's phone app would say, in the hat's own words ("Person on
your left, close by.", "Chair ahead, nearby.", "Something on your right."): people are only ever
"Person", never a name; far things and phones are not said, as on the hat. By default at the
hat's pace (one every info_min_interval_s, 10 s); --say-every 3 is faster for testing (not the hat).
It also says each label key ("Labelling chair") and the result when you quit. (On the hat the Pi
plays no sound: the phone speaks. This is only so a blind tester can run the test.)

Privacy: frames live in memory only. Nothing is saved, recorded or uploaded; only counts are kept.
"""
from __future__ import annotations

import argparse
import json
import os
import shutil
import subprocess
import sys
import time
from collections import Counter

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
from brim_test import detect, load_model  # noqa: E402
from object_test import FALSE_ALARMS_PER_NONE, MIN_DETECTION_RATE, REPO, BrimClasses  # noqa: E402

try:
    import cv2
except ImportError:
    sys.exit('needs OpenCV: .dev/venv/bin/pip install "opencv-python==4.12.0.88"')

KEY_CLASS = {"p": "person", "b": "bicycle", "c": "car", "m": "motorcycle", "u": "bus", "t": "truck", "h": "chair",
             "d": "table", "f": "cell phone", "e": "bench", "k": "backpack", "s": "suitcase", "o": "couch", "n": "none"}
MIN_SECONDS = 5.0  # less than this per class and the summary says so
HELP = ["p person b bicycle c car m motorcycle u bus t truck h chair",
        "d table f phone e bench k backpack s suitcase o couch n none  space off  q quit"]


# The phone's words (app/src/a11y/describe.ts): never a name, never identity.
SUBJECT = {"Person": "Person", "Chair": "Chair", "Table": "Table", "Door": "Door", "Pole": "Pole", "Stairs": "Stairs",
           "Vehicle": "Vehicle", "Bicycle": "Bicycle", "Obstacle": "Something"}
DIRECTION_WORDS = {"left": "on your left", "right": "on your right", "ahead": "ahead"}
PROXIMITY_WORDS = {"close": "close by", "near": "nearby"}


class Speaker:
    """Says what the hat's phone app would say about people and obstacles, using brim's own rules:
    direction from the box centre (nav.json side_margin_frac), closeness from box area
    (safety.json), far things and phones never said, the biggest (closest) thing first."""

    def __init__(self, bc: BrimClasses, cfg: dict, enabled: bool, every_s: float) -> None:
        from object_test import rel
        with open(rel(cfg["safety_config"])) as f:
            safety = json.load(f)
        with open(rel(cfg["nav_config"])) as f:
            nav = json.load(f)
        self.bc = bc
        self.margin = float(nav.get("side_margin_frac", 0.33))
        if not 0 < self.margin < 0.5:
            self.margin = 0.33
        self.person_close = float(safety.get("person_close_area_frac", 0.18))
        self.object_close = float(safety.get("object_close_area_frac", 0.22))
        self.near = float(safety.get("near_area_frac", 0.06))
        self.every_s = every_s if every_s > 0 else max(10.0, float(safety.get("info_min_interval_s", 10)))
        self.enabled = enabled and shutil.which("say") is not None
        if enabled and not self.enabled:
            print("note: no 'say' command on this computer (macOS only); speech is off")
        self.proc = None
        self.last = -1e9

    def phrase(self, d) -> tuple[float, str] | None:
        thing = self.bc.mapping.get(d.class_id)
        kind = self.bc.kinds.get(thing) if thing else None
        if kind is None or d.score < self.bc.thr[d.class_id]:
            return None  # phones and unmapped classes are never said as obstacles
        cx, area = d.x + d.w / 2, d.w * d.h
        side = "left" if cx < self.margin else "right" if cx > 1 - self.margin else "ahead"
        close = self.person_close if thing == "Person" else self.object_close
        prox = "close" if area >= close else "near" if area >= self.near else "far"
        if prox == "far":
            return None  # brim does not mention far things
        parts = [DIRECTION_WORDS[side], PROXIMITY_WORDS[prox]]
        return area, f"{SUBJECT.get(kind, 'Something')} {', '.join(parts)}."

    def speaking(self) -> bool:
        return self.proc is not None and self.proc.poll() is None

    def say(self, text: str, interrupt: bool = False) -> None:
        print(f"           SAY  {text}")
        if not self.enabled:
            return
        if interrupt and self.speaking():
            self.proc.terminate()
        self.proc = subprocess.Popen(["say", text])

    def tick(self, dets, now: float) -> None:
        if now - self.last < self.every_s or self.speaking():
            return
        said = [p for p in (self.phrase(d) for d in dets) if p]
        if not said:
            return
        self.last = now
        self.say(max(said)[1])

    def close(self) -> None:
        if self.speaking():
            self.proc.wait(timeout=15)


def list_cameras() -> None:
    print("Trying camera numbers 0-5 (the C270 is usually 0 or 1 on a Mac; the window shows which):")
    found = 0
    for i in range(6):
        cap = cv2.VideoCapture(i)
        if cap.isOpened():
            ok, frame = cap.read()
            found += ok
            print(f"  {i}: {'opens' if ok else 'opens, no frame'}" + (f", {frame.shape[1]}x{frame.shape[0]}" if ok else ""))
        cap.release()
    if not found:
        print("  none gave a picture. On a Mac, allow camera access for your terminal app (System Settings >\n"
              "  Privacy & Security > Camera), quit and reopen the terminal, then run --list again.")


class Session:
    """Per-label frame counts. Numbers only: no frame, box or image is kept."""

    def __init__(self, bc: BrimClasses) -> None:
        self.bc = bc
        self.frames = Counter()       # label -> frames
        self.seconds = Counter()      # label -> seconds labelled
        self.detected = Counter()     # label -> frames where that class was reported
        self.ms = {}                  # label -> [ms]
        self.also = {}                # label -> Counter of other classes reported in its frames
        self.none_fa = Counter()      # class -> "none" frames where it was reported
        self.phones = 0
        self.phone_hits = Counter()   # obstacle class -> frames where its box sat on a phone
        self.all_ms = []
        self.spoken_result = ""

    def add(self, label, reported: set, ms: float, dt: float, n_phones: int, hits) -> None:
        self.all_ms.append(ms)
        self.phones += n_phones
        for what, _ in hits:
            self.phone_hits[what] += 1
        if label is None:
            return
        self.frames[label] += 1
        self.seconds[label] += dt
        self.ms.setdefault(label, []).append(ms)
        names = {self.bc.name_of[c] for c in reported}
        if label == "none":
            for n in names:
                self.none_fa[n] += 1
            return
        if label in names:
            self.detected[label] += 1
        self.also.setdefault(label, Counter()).update(names - {label})

    def summary(self, elapsed: float) -> int:
        bc = self.bc
        n_none = self.frames["none"]
        fa_allowed = n_none * FALSE_ALARMS_PER_NONE
        print()
        print(f"Session: {elapsed:.0f} s, {len(self.all_ms)} frames, {sum(self.frames.values())} labelled "
              f"({n_none} 'none' frames = {self.seconds['none']:.0f} s)")
        print(f"Per class, per frame  (detected >= {MIN_DETECTION_RATE:.0%}; false alarms <= 1 per 20 'none' frames)")
        hdr = (f"  {'class':<11} {'frames':>6} {'secs':>5} {'detected':>8} {'missed':>6} {'false al.':>9} {'avg ms':>6}"
               f"  {'detection':<16} {'false alarms':<22} also seen most")
        print(hdr)
        print("  " + "-" * (len(hdr) - 2))
        tested, untested, ok_all = [], [], True
        spoken = []
        for c in sorted(bc.mapping, key=lambda c: bc.name_of[c]):
            name = bc.name_of[c]
            fa = self.none_fa[name]
            if not self.frames[name]:
                untested.append(name)
                if n_none and fa > fa_allowed:  # a class can fail on false alarms without being tested itself
                    ok_all = False
                    print(f"  {name:<11} {'-':>6} {'-':>5} {'-':>8} {'-':>6} {fa:>9} {'-':>6}  {'not tested':<16} "
                          f"FAIL {fa}/{n_none}")
                continue
            tested.append(name)
            n, found = self.frames[name], self.detected[name]
            rate = found / n
            det_ok = rate >= MIN_DETECTION_RATE
            fa_txt = (f"{'PASS' if fa <= fa_allowed else 'FAIL'} {fa}/{n_none} = {100.0 * fa / n_none:.1f}%"
                      if n_none else "not tested (press n)")
            ok_all &= det_ok and (not n_none or fa <= fa_allowed)
            avg = sum(self.ms[name]) / n
            top = ", ".join(f"{k} {100.0 * v / n:.0f}%" for k, v in self.also.get(name, Counter()).most_common(2))
            short = "  (short: hold it longer, move around)" if self.seconds[name] < MIN_SECONDS else ""
            spoken.append(f"{name}, {'pass' if det_ok else 'fail'}, found {100.0 * rate:.0f} percent")
            print(f"  {name:<11} {n:>6} {self.seconds[name]:>5.0f} {found:>8} {n - found:>6} {fa:>9} {avg:>6.1f}  "
                  f"{('PASS' if det_ok else 'FAIL') + f' {100.0 * rate:.0f}%':<16} {fa_txt:<22} {top}{short}")
        if n_none:
            seen = ", ".join(f"{k} {v}" for k, v in self.none_fa.most_common()) or "nothing"
            print(f"  {'none':<11} {n_none:>6} {self.seconds['none']:>5.0f}   reported in 'none' frames: {seen}")
        if untested:
            print(f"  not tested: {', '.join(untested)}")

        phone_ok = bc.mapping_phone_ok and not self.phone_hits
        ok_all &= phone_ok
        hits = ", ".join(f"{k} {v}" for k, v in self.phone_hits.items())
        print()
        print(f"  {'PASS' if phone_ok else 'FAIL'}  a cell phone is never reported as an obstacle: mapping "
              f"{'ok' if bc.mapping_phone_ok else 'WRONG'}, {self.phones} phone boxes seen, "
              f"{sum(self.phone_hits.values())} with an obstacle box on them" + (f" ({hits})" if hits else ""))
        avg_all = sum(self.all_ms) / len(self.all_ms) if self.all_ms else float("nan")
        print(f"  detector: {avg_all:.1f} ms per frame on this laptop (the Pi budget is 30 ms; the Pi decides)")
        print()
        self.spoken_result = ("Test finished. Overall " + ("pass. " if ok_all and n_none else "fail. ") + ". ".join(spoken)
                              + ("." if spoken else "") + ("" if n_none else " False alarms not tested.")
                              + ("" if phone_ok else " A phone was seen as an obstacle."))
        if not tested:
            self.spoken_result = "Test finished. No object was labelled."
            print("OVERALL: no object labelled. Press a class key while the object is in view (and n for empty views).")
            return 1
        print(f"OVERALL ({len(tested)} class{'es' if len(tested) != 1 else ''} tested): {'PASS' if ok_all else 'FAIL'}"
              + ("" if n_none else "  (false alarms not tested: label some empty views with n)"))
        return 0 if ok_all and n_none else 1


def draw(frame, dets, bc: BrimClasses, label, live, ms, fps) -> None:
    h, w = frame.shape[:2]
    for d in dets:
        if d.class_id not in bc.mapping or d.score < bc.thr[d.class_id]:
            continue
        name = bc.name_of[d.class_id]
        col = (60, 200, 60) if name == label else (0, 200, 230) if bc.mapping[d.class_id] == "Phone" else (230, 230, 230)
        x0, y0 = int(d.x * w), int(d.y * h)
        cv2.rectangle(frame, (x0, y0), (int((d.x + d.w) * w), int((d.y + d.h) * h)), col, 2)
        thing = bc.mapping[d.class_id]
        tag = f"{name} {d.score:.2f}" + ("" if thing.lower() == name else f" ({'Something' if thing == 'Unknown' else thing})")
        cv2.putText(frame, tag, (x0 + 2, max(16, y0 - 6)), cv2.FONT_HERSHEY_SIMPLEX, 0.55, col, 2)
    cv2.rectangle(frame, (0, 0), (w, 54), (20, 20, 20), -1)
    truth = f"truth: {label}" if label else "truth: not labelling (press a key below)"
    cv2.putText(frame, truth + (f"   {live}" if live else ""), (10, 22), cv2.FONT_HERSHEY_SIMPLEX, 0.62, (255, 255, 255), 2)
    cv2.putText(frame, f"{ms:.1f} ms  {fps:.0f} fps   nothing is saved", (10, 44), cv2.FONT_HERSHEY_SIMPLEX, 0.5,
                (200, 200, 200), 1)
    cv2.rectangle(frame, (0, h - 44), (w, h), (20, 20, 20), -1)
    for i, line in enumerate(HELP):
        cv2.putText(frame, line, (8, h - 26 + 18 * i), cv2.FONT_HERSHEY_SIMPLEX, 0.42, (220, 220, 220), 1)


def main() -> int:
    ap = argparse.ArgumentParser(description="Live per-class detector test on the webcam.")
    ap.add_argument("--camera", default="0", help="camera number (see --list), or a video file to replay")
    ap.add_argument("--list", action="store_true", help="show which camera numbers open, then exit")
    ap.add_argument("--config", default=os.path.join(REPO, "config", "object_test.json"))
    ap.add_argument("--threads", type=int, default=4)
    ap.add_argument("--label", help="label every frame as this class (for replaying a video of one object)")
    ap.add_argument("--no-window", action="store_true", help="no preview window (replaying a video file in a script)")
    ap.add_argument("--say", action="store_true", help="speak what the hat's phone would say, plus label keys and the result")
    ap.add_argument("--say-every", type=float, default=0, metavar="S",
                    help="seconds between spoken sightings (default: the hat's 10 s; min 2)")
    a = ap.parse_args()
    if a.list:
        list_cameras()
        return 0

    with open(a.config) as f:
        cfg = json.load(f)
    bc = BrimClasses(cfg)
    bc.print_mapping(cfg["mapping_source"])
    speaker = Speaker(bc, cfg, a.say, max(a.say_every, 2.0) if a.say_every else 0)
    keys = {ord(k): v for k, v in KEY_CLASS.items() if v == "none" or v in bc.cls_of}
    keys[ord(" ")] = None
    label = a.label
    if label is not None and label != "none" and label not in bc.cls_of:
        sys.exit(f"--label {label!r} is not a class Brim maps: {', '.join(sorted(bc.cls_of))}, none")

    model = load_model(bc.model_path, a.threads)
    src = int(a.camera) if a.camera.isdigit() else a.camera
    cap = cv2.VideoCapture(src)
    if not cap.isOpened():
        sys.exit(f"camera {a.camera} did not open. Try --list. On a Mac, allow camera access for your terminal "
                 "(System Settings > Privacy & Security > Camera) and run again.")
    if isinstance(src, int):  # the hat's frame size
        cap.set(cv2.CAP_PROP_FRAME_WIDTH, 640)
        cap.set(cv2.CAP_PROP_FRAME_HEIGHT, 480)
    print(f"\ncamera {a.camera} open. Click the window, then press the key for what is in view (q quits).")
    if a.say:
        print(f"speaking one sighting every {speaker.every_s:.0f} s" + (" (the hat's pace)" if not a.say_every else " (faster than the hat)"))
        speaker.say("Brim test ready. Press a key for what is in front of the camera.")

    sess = Session(bc)
    t_start = t_prev = time.perf_counter()
    fps = 0.0
    try:
        while True:
            ok, bgr = cap.read()
            if not ok:
                break
            rgb = cv2.cvtColor(bgr, cv2.COLOR_BGR2RGB)
            t0 = time.perf_counter()
            dets = detect(model, rgb)
            ms = (time.perf_counter() - t0) * 1000.0
            best = bc.best_scores(dets)
            reported = {c for c, s in best.items() if s >= bc.thr[c]}
            n_phones, hits = bc.phone_obstacle_hits(dets)
            now = time.perf_counter()
            dt = now - t_prev
            fps = 0.9 * fps + 0.1 / max(dt, 1e-6) if fps else 1.0 / max(dt, 1e-6)
            t_prev = now
            sess.add(label, reported, ms, dt, n_phones, hits)
            if a.say:
                speaker.tick(dets, now)
            if a.no_window:
                continue
            live = ""
            if label == "none":
                live = f"false alarms {sum(sess.none_fa.values())} in {sess.frames['none']} frames"
            elif label:
                n = sess.frames[label]
                live = f"found {sess.detected[label]}/{n} = {100.0 * sess.detected[label] / n:.0f}%" if n else ""
            draw(bgr, dets, bc, label, live, ms, fps)
            cv2.imshow("Brim object test (nothing is saved)", bgr)
            k = cv2.waitKey(1) & 0xFF
            if k in (ord("q"), 27):
                break
            if k in keys:
                label = keys[k]
                print(f"{now - t_start:7.1f} s  truth: {label or 'not labelling'}")
                if a.say:
                    speaker.say("Not labelling." if label is None else "Labelling nothing in view." if label == "none"
                                else f"Labelling {label}.", interrupt=True)
    except KeyboardInterrupt:
        pass
    finally:
        cap.release()
        cv2.destroyAllWindows()
    rc = sess.summary(time.perf_counter() - t_start)
    if a.say:
        speaker.say(sess.spoken_result, interrupt=True)
        speaker.close()
    return rc


if __name__ == "__main__":
    sys.exit(main())
