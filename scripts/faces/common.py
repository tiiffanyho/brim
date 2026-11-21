"""Shared by enrol.py, remove.py and list.py: config, safe paths and the face models.

Face recognition of ENROLLED people only, agreed by the team (Gloriana, Tiffany, Kim); see
CLAUDE.md. Embeddings live only on this machine in config faces_dir (data/faces, git-ignored).
No photo is ever kept. The models are the same ones brim uses on the hat (OpenCV YuNet + SFace,
models/README.md), so an embedding made here matches what the hat computes.
"""
from __future__ import annotations

import json
import os
import re
import sys

REPO = os.path.abspath(os.path.join(os.path.dirname(__file__), "..", ".."))
NAME_RE = re.compile(r"^[A-Za-z][A-Za-z '\-]{0,31}$")  # same rule as faces::valid_name in C++


def load_config() -> dict:
    with open(os.path.join(REPO, "config", "faces.json")) as f:
        return json.load(f)


def faces_dir(cfg: dict) -> str:
    d = cfg["faces_dir"]
    return d if os.path.isabs(d) else os.path.join(REPO, d)


def person_dir(cfg: dict, name: str) -> str:
    """The folder for `name`, refusing anything that is not a plain name (no paths, no tricks)."""
    if not NAME_RE.match(name):
        sys.exit(f"{name!r} is not a plain name (letters first, then letters, spaces, - or ', up to 32)")
    root = os.path.realpath(faces_dir(cfg))
    path = os.path.realpath(os.path.join(root, name))
    if os.path.dirname(path) != root:
        sys.exit("refusing a path outside the faces folder")
    return path


def enrolled(cfg: dict) -> list[str]:
    root = faces_dir(cfg)
    if not os.path.isdir(root):
        return []
    return sorted(n for n in os.listdir(root)
                  if NAME_RE.match(n) and os.path.isfile(os.path.join(root, n, "embeddings.json")))


def models(cfg: dict):
    try:
        import cv2
    except ImportError:
        sys.exit('needs OpenCV: .dev/venv/bin/pip install "opencv-python==4.12.0.88"')
    mdir = os.path.join(REPO, "models")
    det_p, rec_p = os.path.join(mdir, cfg["detector_model"]), os.path.join(mdir, cfg["embedder_model"])
    for p in (det_p, rec_p):
        if not os.path.exists(p):
            sys.exit(f"missing {os.path.relpath(p, REPO)}: see models/README.md (face recognition)")
    det = cv2.FaceDetectorYN.create(det_p, "", (320, 320), float(cfg["face_min_score"]), 0.3, 20)
    rec = cv2.FaceRecognizerSF.create(rec_p, "")
    return cv2, det, rec


def embed_photo(cfg: dict, photo: str):
    """The embedding of the ONE face in a photo. Refuses photos with no face or several faces, so a
    bystander can never be enrolled by mistake. Returns (embedding list, face score, face width px)."""
    cv2, det, rec = models(cfg)
    img = cv2.imread(photo)  # BGR, as the models want
    if img is None:
        sys.exit(f"cannot read {photo}")
    out, err = embed_image(cfg, cv2, det, rec, img)
    if out is None:
        sys.exit(err)
    return out


def embed_image(cfg: dict, cv2, det, rec, img):
    """((embedding, face score, face px), None) for the one face in a BGR image, or (None, reason)."""
    scale = min(1.0, 1280.0 / max(img.shape[:2]))  # big phone photos: shrink so detection is quick
    if scale < 1.0:
        img = cv2.resize(img, (int(img.shape[1] * scale), int(img.shape[0] * scale)))
    det.setInputSize((img.shape[1], img.shape[0]))
    _, faces = det.detect(img)
    faces = [] if faces is None else [f for f in faces if f[2] >= cfg["min_face_px"]]
    if not faces:
        return None, "no clear face (need one face, looking at the camera, not too small)"
    if len(faces) > 1:
        return None, f"{len(faces)} faces: only the person being enrolled may be in view"
    f = faces[0]
    emb = rec.feature(rec.alignCrop(img, f)).reshape(-1)
    return ([round(float(x), 6) for x in emb], float(f[14]), int(f[2])), None


def embed_in_box(cfg: dict, det, rec, bgr, box) -> list[float] | None:
    """The embedding of the face inside a person box (x, y, w, h normalised), or None. Same as
    the hat (device/src/detect/faces.cpp): the top 60% of the box, best face over the score and
    size limits. In memory only; the caller drops it after the frame."""
    h, w = bgr.shape[:2]
    x0, y0 = max(0, int(box[0] * w)), max(0, int(box[1] * h))
    x1, y1 = min(w, int((box[0] + box[2]) * w)), min(h, int((box[1] + box[3] * 0.6) * h))
    if x1 - x0 < cfg["min_face_px"] or y1 - y0 < cfg["min_face_px"]:
        return None
    roi = bgr[y0:y1, x0:x1].copy()
    det.setInputSize((roi.shape[1], roi.shape[0]))
    _, faces = det.detect(roi)
    if faces is None:
        return None
    faces = [f for f in faces if f[2] >= cfg["min_face_px"] and f[14] >= cfg["face_min_score"]]
    if not faces:
        return None
    f = max(faces, key=lambda f: f[14])
    return [float(x) for x in rec.feature(rec.alignCrop(roi, f)).reshape(-1)]
