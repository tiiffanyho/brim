#!/usr/bin/env python3
"""Enrol a person who AGREED to be recognised: python3 scripts/faces/enrol.py <name> <photo>

Computes the face embedding of the one face in <photo> and adds it to data/faces/<name>/
(git-ignored, this machine only). The photo itself is not copied or kept. Run it again with
another photo of the same person to add a second view. Delete with scripts/faces/remove.py.
On the hat, enrol on the hat itself (or copy the folder by hand): nothing is ever synced.

    .dev/venv/bin/python scripts/faces/enrol.py Tiffany ~/Desktop/tiffany.jpg
"""
from __future__ import annotations

import argparse
import json
import os
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from common import embed_photo, load_config, person_dir  # noqa: E402


def main() -> int:
    ap = argparse.ArgumentParser(description="Enrol one person who agreed to be recognised.")
    ap.add_argument("name", help="the name Brim will say, e.g. Tiffany")
    ap.add_argument("photo", help="a photo of only that person, face to the camera")
    a = ap.parse_args()
    cfg = load_config()
    folder = person_dir(cfg, a.name)
    emb, score, width = embed_photo(cfg, a.photo)
    path = os.path.join(folder, "embeddings.json")
    data = {"name": a.name, "model": cfg["embedder_id"], "dim": len(emb), "embeddings": []}
    if os.path.exists(path):
        with open(path) as f:
            old = json.load(f)
        if old.get("model") == cfg["embedder_id"] and old.get("name") == a.name:
            data["embeddings"] = old.get("embeddings", [])
    data["embeddings"].append(emb)
    os.makedirs(folder, mode=0o700, exist_ok=True)
    tmp = path + ".tmp"
    with open(tmp, "w") as f:
        json.dump(data, f)
    os.chmod(tmp, 0o600)
    os.replace(tmp, path)
    print(f"enrolled {a.name}: {len(data['embeddings'])} view(s) (face score {score:.2f}, {width} px wide). "
          f"The photo was not kept. Remove with: scripts/faces/remove.py {a.name}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
