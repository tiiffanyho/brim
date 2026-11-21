#!/usr/bin/env python3
"""Enrol a person who AGREED to be recognised:
    python3 scripts/faces/enrol.py <name> <photo>          (a photo of only them)
    python3 scripts/faces/enrol.py <name> --camera 0       (best: the hat's own C270, at hat height)

Computes the face embedding of the one face in <photo> and adds it to data/faces/<name>/
(git-ignored, this machine only). The photo itself is not copied or kept. Run it again with
another photo of the same person to add a second view. Delete with scripts/faces/remove.py.
On the hat, enrol on the hat itself (or copy the folder by hand): nothing is ever synced.

    .dev/venv/bin/python scripts/faces/enrol.py Tiffany ~/Desktop/tiffany.jpg
    .dev/venv/bin/python scripts/faces/enrol.py Tiffany --camera 0 --replace

With --camera, the person looks at the camera for a few seconds, turning their head a little; the
embeddings of up to --views different frames are kept. No frame is saved. Enrolling through the
same camera the hat uses matches far better than a phone photo.
"""
from __future__ import annotations

import argparse
import json
import os
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from common import embed_image, embed_photo, load_config, models, person_dir  # noqa: E402


def from_camera(cfg: dict, camera: str, views: int, seconds: float) -> list:
    """Embeddings of `views` different moments from the webcam, one face only. No frame is kept."""
    import time
    cv2, det, rec = models(cfg)
    cap = cv2.VideoCapture(int(camera) if camera.isdigit() else camera)
    if not cap.isOpened():
        sys.exit(f"camera {camera} did not open (allow camera access for your terminal, or try another number)")
    cap.set(cv2.CAP_PROP_FRAME_WIDTH, 640)
    cap.set(cv2.CAP_PROP_FRAME_HEIGHT, 480)
    print(f"look at camera {camera}; turn your head a little left and right ({seconds:.0f} s)")
    out, last_err, t0, last_t = [], "", time.time(), 0.0
    while time.time() - t0 < seconds and len(out) < views:
        ok, frame = cap.read()
        if not ok:
            break
        if time.time() - last_t < seconds / (views + 1):  # spread the views out in time
            continue
        got, err = embed_image(cfg, cv2, det, rec, frame)
        if got is None:
            last_err = err
            continue
        last_t = time.time()
        out.append(got)
        print(f"  view {len(out)}: face score {got[1]:.2f}, {got[2]} px wide")
    cap.release()
    if not out:
        sys.exit(f"no usable view: {last_err}")
    return out


def main() -> int:
    ap = argparse.ArgumentParser(description="Enrol one person who agreed to be recognised.")
    ap.add_argument("name", help="the name Brim will say, e.g. Tiffany")
    ap.add_argument("photo", nargs="?", help="a photo of only that person, face to the camera")
    ap.add_argument("--camera", help="enrol from this webcam number (or a video file) instead of a photo; no frame is saved")
    ap.add_argument("--views", type=int, default=5, help="with --camera: how many different moments to keep")
    ap.add_argument("--seconds", type=float, default=6.0, help="with --camera: how long to look")
    ap.add_argument("--replace", action="store_true", help="drop this person's earlier views first")
    a = ap.parse_args()
    if (a.photo is None) == (a.camera is None):
        ap.error("give a photo or --camera N (one of them)")
    cfg = load_config()
    folder = person_dir(cfg, a.name)
    got = from_camera(cfg, a.camera, max(1, min(a.views, 10)), a.seconds) if a.camera is not None else [embed_photo(cfg, a.photo)]
    path = os.path.join(folder, "embeddings.json")
    data = {"name": a.name, "model": cfg["embedder_id"], "dim": len(got[0][0]), "embeddings": []}
    if os.path.exists(path) and not a.replace:
        with open(path) as f:
            old = json.load(f)
        if old.get("model") == cfg["embedder_id"] and old.get("name") == a.name:
            data["embeddings"] = old.get("embeddings", [])
    data["embeddings"].extend(g[0] for g in got)
    score, width = got[-1][1], got[-1][2]
    os.makedirs(folder, mode=0o700, exist_ok=True)
    tmp = path + ".tmp"
    with open(tmp, "w") as f:
        json.dump(data, f)
    os.chmod(tmp, 0o600)
    os.replace(tmp, path)
    print(f"enrolled {a.name}: {len(data['embeddings'])} view(s) (face score {score:.2f}, {width} px wide). "
          "No photo or frame was kept. Remove with: scripts/faces/remove.py " + a.name)
    return 0


if __name__ == "__main__":
    sys.exit(main())
