#!/usr/bin/env python3
"""Object accuracy test on labelled photos: how well does the hat's detector find each class Brim maps?

A laptop accuracy test, not a feature: nothing here speaks, and no phrase is added. The classes come
from Brim's own COCO-to-Thing mapping (device/src/detect/detector.cpp, read at run time, never
edited here), checked against the label list inside the model file. The detector is
scripts/laptop/brim_test.py (same model, input and outputs as the hat); the thresholds are the
hat's own (config/safety.json, read-only). Paths: config/object_test.json.

    .dev/venv/bin/pip install "ai-edge-litert==1.2.0" numpy pillow
    .dev/venv/bin/python scripts/laptop/object_test.py data/object_test
    .dev/venv/bin/python scripts/laptop/object_test.py data/object_test --sweep   # thresholds, config unchanged

data/object_test/ (git-ignored) holds the hat-camera photos and labels.csv:

    photo,label,notes
    IMG_0001.jpg,chair,2 m, hat height
    IMG_0101.jpg,none,empty corridor

label is one class name (person, bicycle, car, motorcycle, bus, truck, chair, table, cell phone,
bench, backpack, suitcase, couch) or none. A photo may show other classes too (a teammate next to a
bicycle); only its label is scored, and false alarms are counted on "none" photos only, so a "none"
photo must show nothing from the list, people included. Only teammates who agreed to appear;
vehicles parked or on private property, never on a public road or near traffic. Photos are only
read: nothing is written, uploaded or kept. Exit code 0 only when every criterion passes.
"""
from __future__ import annotations

import argparse
import csv
import json
import os
import re
import sys
import time
import zipfile

HERE = os.path.dirname(os.path.abspath(__file__))
REPO = os.path.abspath(os.path.join(HERE, "..", ".."))
sys.path.insert(0, HERE)
from brim_test import detect, load_image, load_model  # noqa: E402

MIN_DETECTION_RATE = 0.85
FALSE_ALARMS_PER_NONE = 1 / 20  # at most 1 false alarm in every 20 "none" photos
MIN_PHOTOS_PER_CLASS = 20
MIN_NONE_PHOTOS = 60
PHONE_OVERLAP_IOU = 0.5          # an obstacle box this much on top of a phone box = phone called an obstacle
ALIASES = {"dining table": "table", "phone": "cell phone", "cellphone": "cell phone", "motorbike": "motorcycle"}


def rel(p: str) -> str:
    return os.path.join(REPO, p) if not os.path.isabs(p) else p


# ------------------------------------------------------------------ Brim's mapping, read from the C++ source
def function_body(src: str, signature: str) -> str:
    i = src.find(signature)
    if i < 0:
        sys.exit(f"could not find {signature!r} in the mapping source: has it been renamed?")
    j, depth = src.index("{", i), 0
    for k in range(j, len(src)):
        depth += {"{": 1, "}": -1}.get(src[k], 0)
        if depth == 0:
            return src[j:k]
    sys.exit(f"unbalanced braces after {signature!r}")


def read_mapping(det_src: str) -> dict[int, str]:
    """thing_of_coco(): COCO-90 index -> Thing name, exactly as the hat maps it."""
    body = re.sub(r"//[^\n]*", "", function_body(det_src, "std::optional<Thing> thing_of_coco("))
    out, pending = {}, []
    for tok in re.finditer(r"case\s+(\d+)\s*:|return\s+Thing::(\w+)\s*;|default\s*:", body):
        if tok.group(1) is not None:
            pending.append(int(tok.group(1)))
        elif tok.group(2) is not None:
            for c in pending:
                out[c] = tok.group(2)
            pending = []
        else:
            pending = []
    if not out:
        sys.exit("read no classes from thing_of_coco(): the mapping source format changed")
    return out


def read_obstacle_kinds(det_src: str) -> dict[str, str | None]:
    """obstacle_kind_of(): Thing -> what it is called as an obstacle (None = never an obstacle)."""
    body = function_body(det_src, "obstacle_kind_of(Thing t)")
    out = {}
    for m in re.finditer(r"case\s+Thing::(\w+)\s*:\s*return\s+(?:schema::ObstacleKind::(\w+)|std::nullopt)", body):
        out[m.group(1)] = m.group(2)
    return out


def all_things(det_src: str) -> list[str]:
    m = re.search(r"enum class Thing\s*\{([^}]*)\}", det_src)
    return [t.strip() for t in m.group(1).split(",") if t.strip()] if m else []


def nav_ignores_phones(nav_src: str, det_src: str) -> bool:
    if re.search(r"Thing::Phone\s*\)\s*continue", nav_src):
        return True
    if "is_obstacle(d.thing)" in nav_src:
        m = re.search(r"bool is_obstacle\(Thing t\)\s*\{([^}]*)\}", det_src)
        return bool(m and "t != Thing::Phone" in m.group(1))
    return False


def model_labels(model_path: str) -> list[str]:
    """The labelmap.txt packed inside the .tflite (Model Maker export)."""
    try:
        with zipfile.ZipFile(model_path) as z:
            return z.read("labelmap.txt").decode().splitlines()
    except (zipfile.BadZipFile, KeyError):
        return []


def spoken(thing: str, kinds: dict[str, str | None]) -> str:
    if thing == "Phone":
        return "never an obstacle (safety: phone pointed at you)"
    k = kinds.get(thing)
    if k is None:
        return "never an obstacle"
    return '"Something"' if k == "Obstacle" else f'"{k}"'


class BrimClasses:
    """Everything the tests need to score like the hat: mapping, thresholds, names, obstacle classes."""

    def __init__(self, cfg: dict) -> None:
        self.model_path = rel(cfg["model"])
        with open(rel(cfg["mapping_source"])) as f:
            self.det_src = f.read()
        with open(rel(cfg["nav_source"])) as f:
            self.nav_src = f.read()
        header = os.path.splitext(rel(cfg["mapping_source"]))[0] + ".hpp"  # enum class Thing lives here
        hdr_src = open(header).read() if os.path.exists(header) else ""
        with open(rel(cfg["safety_config"])) as f:
            safety = json.load(f)
        min_conf = float(safety.get("min_confidence", 0.60))
        phone_conf = float(safety.get("phone_min_confidence", 0.55))
        self.mapping = read_mapping(self.det_src)
        self.kinds = read_obstacle_kinds(self.det_src)
        self.labels = model_labels(self.model_path)
        lab = self.labels
        self.name_of = {c: ALIASES.get(lab[c], lab[c]) if c < len(lab) else f"class {c}" for c in self.mapping}
        self.cls_of = {n: c for c, n in self.name_of.items()}
        self.thr = {c: phone_conf if self.mapping[c] == "Phone" else min_conf for c in self.mapping}
        self.obstacle_classes = {c for c in self.mapping if self.kinds.get(self.mapping[c]) is not None}
        self.phone_classes = [c for c in self.mapping if self.mapping[c] == "Phone"]
        self.mapping_phone_ok = (bool(self.phone_classes) and self.kinds.get("Phone", "missing") is None
                                 and nav_ignores_phones(self.nav_src, self.det_src))
        self.notes = []
        for c in self.mapping:
            if c >= len(lab) or lab[c] in ("???", ""):
                self.notes.append(f"COCO {c} is a gap in the model's labelmap but is mapped to {self.mapping[c]}")
        unreachable = [t for t in all_things(hdr_src + self.det_src) if t not in set(self.mapping.values()) and t != "Unknown"]
        if unreachable:
            self.notes.append(f"Thing::{', Thing::'.join(unreachable)} have no COCO class: the detector can never report "
                              "them (only fake mode does). Not changed; reported for the team.")
        if not lab:
            self.notes.append("the model has no labelmap.txt inside; class names could not be checked")

    def print_mapping(self, source: str) -> None:
        print(f"Brim's COCO-to-Thing mapping ({source}), checked against the model's labelmap.txt:")
        print(f"  {'COCO':>4}  {'model label':<14} {'Brim Thing':<10} {'threshold':>9}  said as")
        for c in sorted(self.mapping):
            lab = self.labels[c] if c < len(self.labels) else "?"
            print(f"  {c:>4}  {lab:<14} {self.mapping[c]:<10} {self.thr[c]:>9.2f}  {spoken(self.mapping[c], self.kinds)}")
        for n in self.notes:
            print(f"  note: {n}")

    def best_scores(self, dets) -> dict[int, float]:
        best = {}
        for d in dets:
            if d.class_id in self.mapping and d.score > best.get(d.class_id, -1):
                best[d.class_id] = d.score
        return best

    def phone_obstacle_hits(self, dets) -> tuple[int, list]:
        """(phones seen, [(obstacle class, score)] boxes sitting on a phone): a phone is never an obstacle."""
        phones = [d for d in dets if d.class_id in self.phone_classes and d.score >= self.thr[d.class_id]]
        hits = [(self.name_of[d.class_id], d.score) for p in phones for d in dets
                if d.class_id in self.obstacle_classes and d.score >= self.thr[d.class_id] and iou(p, d) >= PHONE_OVERLAP_IOU]
        return len(phones), hits


# ------------------------------------------------------------------ the photo test
def iou(a, b) -> float:
    x1, y1 = max(a.x, b.x), max(a.y, b.y)
    x2, y2 = min(a.x + a.w, b.x + b.w), min(a.y + a.h, b.y + b.h)
    inter = max(0.0, x2 - x1) * max(0.0, y2 - y1)
    union = a.w * a.h + b.w * b.h - inter
    return inter / union if union > 0 else 0.0


def read_labels(path: str, names: set[str]) -> list[tuple[str, str]]:
    if not os.path.exists(path):
        sys.exit(f"no labels file: {path}\n(columns: photo,label with label one of: {', '.join(sorted(names))}, none)")
    rows = []
    with open(path, newline="") as f:
        r = csv.DictReader(f)
        if not r.fieldnames or "photo" not in r.fieldnames or "label" not in r.fieldnames:
            sys.exit(f"{path}: the first line must be a header with 'photo' and 'label'")
        for i, row in enumerate(r, start=2):
            photo = (row.get("photo") or "").strip()
            label = (row.get("label") or "").strip().lower()
            label = ALIASES.get(label, label)
            if not photo:
                continue
            if label != "none" and label not in names:
                sys.exit(f"{path}:{i}: label {label!r} is not a class Brim maps: {', '.join(sorted(names))}, none")
            rows.append((photo, label))
    return rows


def main() -> int:
    ap = argparse.ArgumentParser(description="Per-class detector accuracy on labelled photos.")
    ap.add_argument("data_dir", nargs="?", help="default: data_dir in config/object_test.json")
    ap.add_argument("--config", default=os.path.join(REPO, "config", "object_test.json"))
    ap.add_argument("--threads", type=int, default=4)
    ap.add_argument("--sweep", action="store_true", help="also show each class at other thresholds (config is not changed)")
    a = ap.parse_args()

    with open(a.config) as f:
        cfg = json.load(f)
    data_dir = a.data_dir or rel(cfg["data_dir"])
    bc = BrimClasses(cfg)
    model_path, mapping, name_of, cls_of, thr = bc.model_path, bc.mapping, bc.name_of, bc.cls_of, bc.thr

    # 1. The mapping, as the hat has it.
    bc.print_mapping(cfg["mapping_source"])
    print()

    rows = read_labels(os.path.join(data_dir, cfg.get("labels_file", "labels.csv")), set(cls_of))
    if not rows:
        sys.exit("labels.csv has no photos")
    model = load_model(model_path, a.threads)

    # 2. One row per photo.
    hdr = f"{'photo':<28} {'label':<11} {'detected':<8} {'score':>5}  {'correct':<7} {'ms':>6}  also seen"
    print(hdr)
    print("-" * len(hdr))
    results, missing, phone_as_obstacle = [], [], []
    phones_checked = 0
    for photo, label in rows:
        path = os.path.join(data_dir, photo)
        if not os.path.exists(path):
            missing.append(photo)
            print(f"{photo:<28} {label:<11} MISSING")
            continue
        rgb = load_image(path)  # not timed: the hat's camera hands over RGB directly
        t0 = time.perf_counter()
        dets = detect(model, rgb)
        ms = (time.perf_counter() - t0) * 1000.0
        best = bc.best_scores(dets)  # COCO class -> best score, mapped classes only
        reported = {c for c, s in best.items() if s >= thr[c]}
        if label == "none":
            detected = bool(reported)
            score = max((best[c] for c in reported), default=max(best.values(), default=None))
            correct = not detected
            seen = sorted(name_of[c] for c in reported)
        else:
            c = cls_of[label]
            detected = c in reported
            score = best.get(c)
            correct = detected
            seen = sorted(name_of[x] for x in reported if x != c)
        # A phone is never an obstacle: no obstacle-class box may sit on a phone box.
        n_phones, hits = bc.phone_obstacle_hits(dets)
        phones_checked += n_phones
        phone_as_obstacle += [(photo, what, sc) for what, sc in hits]
        results.append({"photo": photo, "label": label, "detected": detected, "correct": correct, "ms": ms,
                        "reported": reported, "best": best})
        s = f"{score:.2f}" if score is not None else "-"
        print(f"{photo:<28} {label:<11} {'yes' if detected else 'no':<8} {s:>5}  {'yes' if correct else 'no':<7} "
              f"{ms:6.1f}  {', '.join(seen) if seen else ''}")

    # 3. Per class.
    none = [r for r in results if r["label"] == "none"]
    n_none = len(none)
    fa_allowed = n_none * FALSE_ALARMS_PER_NONE
    print()
    print(f"Per class  (detection >= {MIN_DETECTION_RATE:.0%} of its photos; false alarms <= 1 per 20 'none' photos = "
          f"{fa_allowed:.1f} of {n_none}; at least {MIN_PHOTOS_PER_CLASS} photos per class)")
    hdr = f"  {'class':<11} {'photos':>6} {'detected':>8} {'missed':>6} {'false alarms':>12} {'avg ms':>7}  verdict"
    print(hdr)
    print("  " + "-" * (len(hdr) - 2))
    all_ok = True
    for c in sorted(mapping, key=lambda c: name_of[c]):
        name = name_of[c]
        mine = [r for r in results if r["label"] == name]
        found = sum(r["detected"] for r in mine)
        fa = sum(c in r["reported"] for r in none)
        avg_ms = sum(r["ms"] for r in mine) / len(mine) if mine else float("nan")
        why = []
        if len(mine) < MIN_PHOTOS_PER_CLASS:
            why.append(f"{len(mine)} of {MIN_PHOTOS_PER_CLASS} photos")
        if not mine or found / len(mine) < MIN_DETECTION_RATE:
            why.append("detection")
        if n_none == 0 or fa > fa_allowed:
            why.append("false alarms")
        ok = not why
        all_ok &= ok
        rate = f"{100.0 * found / len(mine):.0f}%" if mine else "n/a"
        ms_s = f"{avg_ms:7.1f}" if mine else f"{'-':>7}"
        print(f"  {name:<11} {len(mine):>6} {found:>8} {len(mine) - found:>6} {fa:>12} {ms_s}  "
              f"{'PASS' if ok else 'FAIL'}  det {found}/{len(mine)} = {rate}, false alarms {fa}/{n_none}"
              + (f"  ({', '.join(why)})" if why else ""))
    none_ok = sum(r["correct"] for r in none)
    print(f"  {'none':<11} {n_none:>6} {'':>8} {'':>6} {n_none - none_ok:>12}           "
          f"{none_ok}/{n_none} 'none' photos with nothing reported")

    # 4. Cell phone is never an obstacle.
    phone_cls = bc.phone_classes
    mapping_ok = bc.mapping_phone_ok
    phone_ok = mapping_ok and not phone_as_obstacle
    all_ok &= phone_ok
    print()
    print(f"  {'PASS' if phone_ok else 'FAIL'}  a cell phone is never reported as an obstacle: "
          f"mapping {'ok' if mapping_ok else 'WRONG'} (COCO {', '.join(map(str, phone_cls)) or '-'} -> Phone, "
          f"obstacle_kind_of(Phone) = none, nav skips phones), "
          f"{len(phone_as_obstacle)} of {phones_checked} detected phones had an obstacle box on them (IoU >= {PHONE_OVERLAP_IOU})")
    for photo, what, sc in phone_as_obstacle:
        print(f"        {photo}: phone also seen as {what} ({sc:.2f})")

    if a.sweep:
        print()
        print("Threshold sweep (config/safety.json is NOT changed; report the best, do not push tuning)")
        ts = [0.3, 0.4, 0.5, 0.6, 0.7]
        print(f"  {'class':<11} " + " ".join(f"{'@' + str(t):>13}" for t in ts) + "   (detected% / false alarms)")
        for c in sorted(mapping, key=lambda c: name_of[c]):
            mine = [r for r in results if r["label"] == name_of[c]]
            cells = []
            for t in ts:
                f_ = sum(r["best"].get(c, -1) >= t for r in mine)
                fa = sum(r["best"].get(c, -1) >= t for r in none)
                cells.append(f"{(100.0 * f_ / len(mine)) if mine else 0:5.0f}% / {fa:<3}")
            print(f"  {name_of[c]:<11} " + " ".join(f"{x:>13}" for x in cells))

    print()
    if missing:
        print(f"MISSING photos (listed in labels.csv, not found): {', '.join(missing)}")
    if n_none < MIN_NONE_PHOTOS:
        print(f"note: the test plan asks for at least {MIN_NONE_PHOTOS} 'none' photos (have {n_none})")
    ok = all_ok and not missing
    print(f"OVERALL: {'PASS' if ok else 'FAIL'}")
    return 0 if ok else 1


if __name__ == "__main__":
    sys.exit(main())
