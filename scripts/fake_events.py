#!/usr/bin/env python3
"""Injects realistic fake events straight into app_server's event channel — no C++ brim needed.

Useful when working on the PWA alone:  run app_server, then  scripts/fake_events.py
Speaks the same Unix-datagram framing as device/src/ipc/bus_uds.cpp: b"E" + JSON.
Events follow shared/events.schema.json: person_seen, obstacle_seen, clear_path, safety_alert,
fault and health. Nobody is identified — people are anonymous track ids.
Laptop only (on the Pi, brim uses QNX message passing).
"""
import argparse
import json
import os
import random
import socket
import time

DIRECTIONS = ["left", "ahead", "right"]
OBSTACLES = ["chair", "table", "door", "pole", "stairs", "bicycle", "obstacle"]
SAFETY = [
    ("person_close", "critical", "Person close ahead"),
    ("object_close", "warning", "Chair close ahead"),
    ("object_close", "critical", "Wall close ahead"),
    ("phone_raised", "warning", "A phone is pointed at you"),
]
# Navigation advice: a direction we are sure about, or "stop" when nothing is clear.
PATHS = [
    (True, "ahead", "Clear path ahead", []),
    (True, "right", "Chair ahead, step right", ["ahead"]),
    (True, "left", "Person close ahead, step left", ["ahead", "right"]),
    (False, "unknown", "Path unclear, stop", ["left", "ahead", "right"]),
]


def mono_ms() -> int:
    return time.clock_gettime_ns(time.CLOCK_MONOTONIC) // 1_000_000  # same clock as the C++ code


class Sender:
    def __init__(self, run_dir: str):
        self.path = os.path.join(run_dir, "brim_events.sock")
        self.sock = socket.socket(socket.AF_UNIX, socket.SOCK_DGRAM)
        self.seq = 0
        self.track = 0
        self.start = mono_ms()

    def send(self, event: dict) -> None:
        self.seq += 1
        now = mono_ms()
        event = {"seq": self.seq, "source": "fake_events", "t_mono_ms": now, **event}
        if event["type"] not in ("fault", "health"):
            event.setdefault("t_capture_mono_ms", now - random.randint(25, 70))
        try:
            self.sock.sendto(b"E" + json.dumps(event).encode(), self.path)
            summary = event.get("text") or " ".join(
                str(event[k]) for k in ("label", "direction", "proximity") if k in event
            )
            print(f"-> {event['type']:16} {summary}")
        except (FileNotFoundError, ConnectionRefusedError):
            print(f"app_server not listening on {self.path} (is it running with the same BRIM_RUN_DIR?)")

    def next_track(self) -> int:
        self.track += 1
        return self.track

    def health(self) -> None:
        self.send({"type": "health", "uptime_s": (mono_ms() - self.start) / 1000, "fps": round(random.uniform(14.2, 15.0), 1),
                   "cpu_pct": round(random.uniform(30, 55), 1), "temp_c": round(random.uniform(48, 61), 1),
                   "pipeline_latency_ms": round(random.uniform(35, 70), 1), "speak_queue_len": random.randint(0, 2),
                   "components": [{"name": n, "state": "stub", "detail": "fake_events.py"} for n in
                                  ("capture", "detect", "safety", "speech")]})


def random_event(s: Sender) -> None:
    r = random.random()
    area = round(random.uniform(0.03, 0.30), 3)
    proximity = "close" if area >= 0.18 else "near" if area >= 0.06 else "far"
    if r < 0.3:
        s.send({"type": "person_seen", "track_id": s.next_track(), "direction": random.choice(DIRECTIONS),
                "proximity": proximity, "confidence": round(random.uniform(0.82, 0.98), 2), "area_frac": area})
    elif r < 0.55:
        s.send({"type": "obstacle_seen", "track_id": s.next_track(), "label": random.choice(OBSTACLES),
                "direction": random.choice(DIRECTIONS), "proximity": proximity,
                "confidence": round(random.uniform(0.65, 0.95), 2), "area_frac": area})
    elif r < 0.75:
        clear, direction, text, blocked = random.choice(PATHS)
        s.send({"type": "clear_path", "clear": clear, "direction": direction, "text": text, "blocked": blocked})
    elif r < 0.95:
        kind, severity, text = random.choice(SAFETY)
        s.send({"type": "safety_alert", "kind": kind, "severity": severity, "text": text, "direction": "ahead",
                "proximity": "close", "confidence": round(random.uniform(0.8, 0.99), 2)})
    else:
        s.send({"type": "fault", "component": "camera", "action": "degraded", "message": "frame rate dropped (fake)"})


def main() -> None:
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--run-dir", default=os.environ.get("BRIM_RUN_DIR", "/tmp/brim"))
    ap.add_argument("--interval", type=float, default=3.0, help="seconds between random events (health is 1 Hz)")
    ap.add_argument("--count", type=int, default=0, help="stop after N random events (0 = forever)")
    args = ap.parse_args()
    s = Sender(args.run_dir)
    print(f"sending to {s.path} every {args.interval}s (Ctrl-C to stop)")
    sent, next_event = 0, time.monotonic()
    try:
        while args.count == 0 or sent < args.count:
            s.health()
            if time.monotonic() >= next_event:
                random_event(s)
                sent += 1
                next_event = time.monotonic() + args.interval
            time.sleep(1.0)
    except KeyboardInterrupt:
        pass


if __name__ == "__main__":
    main()
