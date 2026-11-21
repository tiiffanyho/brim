#!/usr/bin/env python3
"""Delete an enrolled person: python3 scripts/faces/remove.py <name>

Deletes data/faces/<name>/ and every embedding in it. A running brim reloads the faces folder
within gallery_reload_ms (config/faces.json) and never says that name again.
"""
from __future__ import annotations

import argparse
import os
import shutil
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from common import load_config, person_dir  # noqa: E402


def main() -> int:
    ap = argparse.ArgumentParser(description="Delete one enrolled person and their embeddings.")
    ap.add_argument("name")
    a = ap.parse_args()
    folder = person_dir(load_config(), a.name)
    if not os.path.isdir(folder):
        print(f"{a.name} is not enrolled (nothing to delete)")
        return 1
    shutil.rmtree(folder)
    print(f"deleted {a.name} and their embeddings")
    return 0


if __name__ == "__main__":
    sys.exit(main())
