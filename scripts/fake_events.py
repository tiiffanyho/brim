#!/usr/bin/env python3
"""Injects realistic fake events straight into app_server's event channel — no C++ brim needed.

Useful when working on the PWA alone:  run app_server, then  scripts/fake_events.py
Speaks the same Unix-datagram framing as device/src/ipc/bus_uds.cpp: b"E" + JSON.
Events follow shared/events.schema.json (person/cue/safety/fault/health/unknown_person).
Laptop only (on the Pi, brim uses QNX message passing).
"""
import argparse
import json
import os
import random
import socket
import time

PEOPLE = [(1, "Sam"), (2, "Priya"), (3, "Alex")]  # ids match `app_server --seed-demo`
DIRECTIONS = ["left", "ahead", "right"]
CUES = [("waving", "{} is waving"), ("smiling", "{} is smiling at you"), ("approaching", "{} is coming toward you"),
        ("leaving", "{} is leaving"), ("looking_at_you", "{} is looking at you")]
SAFETY = [("obstacle_head_height", "critical", "Low branch ahead"), ("step_or_curb", "warning", "Step down ahead"),
          ("fast_approach", "warning", "Bike coming fast on your left"), ("vehicle", "critical", "Car reversing ahead"),
          ("obstacle_ahead", "info", "Chair ahead")]


def mono_ms() -> int:
    return time.clock_gettime_ns(time.CLOCK_MONOTONIC) // 1_000_000  # same clock as the C++ code


class Sender:
    def __init__(self, run_dir: str):
        self.path = os.path.join(run_dir, "brim_events.sock")
        self.sock = socket.socket(socket.AF_UNIX, socket.SOCK_DGRAM)
        self.seq = 0
        self.start = mono_ms()

    def send(self, event: dict) -> None:
        self.seq += 1
        now = mono_ms()
        event = {"seq": self.seq, "source": "fake_events", "t_mono_ms": now, **event}
        if event["type"] not in ("fault", "health"):
            event.setdefault("t_capture_mono_ms", now - random.randint(25, 70))
        try:
            self.sock.sendto(b"E" + json.dumps(event).encode(), self.path)
            print(f"-> {event['type']:18} {event.get('display_name') or event.get('text') or event.get('anon_id') or ''}")
        except (FileNotFoundError, ConnectionRefusedError):
            print(f"app_server not listening on {self.path} (is it running with the same BRIM_RUN_DIR?)")

    def health(self) -> None:
        self.send({"type": "health", "uptime_s": (mono_ms() - self.start) / 1000, "fps": round(random.uniform(14.2, 15.0), 1),
                   "cpu_pct": round(random.uniform(30, 55), 1), "temp_c": round(random.uniform(48, 61), 1),
                   "pipeline_latency_ms": round(random.uniform(35, 70), 1), "speak_queue_len": random.randint(0, 2),
                   "components": [{"name": n, "state": "stub", "detail": "fake_events.py"} for n in
                                  ("capture", "faces", "recognize", "cues", "safety", "audio")]})


def random_event(s: Sender) -> None:
    r = random.random()
    pid, name = random.choice(PEOPLE)
    if r < 0.35:
        s.send({"type": "person_recognized", "person_id": pid, "display_name": name,
                "confidence": round(random.uniform(0.82, 0.98), 2), "direction": random.choice(DIRECTIONS)})
    elif r < 0.55:
        kind, text = random.choice(CUES)
        s.send({"type": "cue", "cue": kind, "text": text.format(name), "person_id": pid})
    elif r < 0.75:
        s.send({"type": "unknown_person", "anon_id": f"stranger-{random.randint(1, 5)}", "direction": random.choice(DIRECTIONS)})
    elif r < 0.95:
        kind, sev, text = random.choice(SAFETY)
        s.send({"type": "safety_alert", "kind": kind, "severity": sev, "text": text,
                "distance_m": round(random.uniform(0.8, 3.0), 1), "direction": "ahead"})
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
