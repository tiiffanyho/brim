#!/usr/bin/env python3
"""List enrolled people (names only): python3 scripts/faces/list.py"""
from __future__ import annotations

import os
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from common import enrolled, load_config  # noqa: E402

names = enrolled(load_config())
print("\n".join(names) if names else "nobody is enrolled (no face model runs)")
