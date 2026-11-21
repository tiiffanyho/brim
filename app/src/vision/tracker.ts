// Anonymous box tracker + distance estimate for the laptop camera test. Same matching rules as
// config/detect.json (box overlap or a short centre jump within a time window). A track is just a
// throw-away number and a few seconds of box sizes, held in memory only.
import detectJson from "../../../config/detect.json";
import type { Box, Detection, Thing } from "./nav";

const IOU = detectJson.track_match_iou; // 0.3
const WINDOW_MS = detectJson.track_match_window_ms; // 1500
const CENTER_GATE = detectJson.track_center_gate; // 0.6 (in box sizes)
const HISTORY_MS = 1500;

/**
 * Typical real heights in metres, used for the pinhole distance estimate. Rough on purpose:
 * there is no distance sensor, so every distance here is an estimate.
 */
export const REAL_HEIGHT_M: Partial<Record<string, number>> = {
  person: 1.7,
  chair: 0.9,
  table: 0.75,
  vehicle: 1.5,
  bicycle: 1.0,
  phone: 0.15,
  unknown: 0.8,
  bottle: 0.25,
  cup: 0.1,
  laptop: 0.25,
  tv: 0.5,
  backpack: 0.5,
};

export interface Track {
  id: number;
  thing: Thing;
  label: string; // the detector's own name, e.g. "person", "cup"
  box: Box;
  score: number;
  lastSeen: number;
  /** Box height fraction over time, newest last. */
  history: { t: number; h: number; area: number }[];
  distanceM: number | null;
  /** Metres per second towards the camera (positive = getting closer). */
  approachMps: number;
  /** Seconds until it reaches the camera at this speed, if it is approaching. */
  timeToContactS: number | null;
  approaching: boolean;
}

function iou(a: Box, b: Box): number {
  const ix = Math.max(0, Math.min(a.x + a.w, b.x + b.w) - Math.max(a.x, b.x));
  const iy = Math.max(0, Math.min(a.y + a.h, b.y + b.h) - Math.max(a.y, b.y));
  const inter = ix * iy;
  const u = a.w * a.h + b.w * b.h - inter;
  return u > 0 ? inter / u : 0;
}
function centreJump(a: Box, b: Box): number {
  const dx = a.x + a.w / 2 - (b.x + b.w / 2);
  const dy = a.y + a.h / 2 - (b.y + b.h / 2);
  const size = Math.max(a.w, a.h, 1e-6);
  return Math.hypot(dx, dy) / size;
}

/**
 * Pinhole camera: distance = real height × focal length / height in pixels.
 * focalFrac is the focal length as a fraction of frame height (from the field of view, or from
 * "calibrate at 1 m"). Boxes cut off by the frame edge make this too far: treat as a lower bound.
 */
export function estimateDistanceM(label: string, box: Box, focalFrac: number): number | null {
  const real = REAL_HEIGHT_M[label];
  if (!real || box.h <= 0.01) return null;
  return (real * focalFrac) / box.h;
}

/** Focal length (as a fraction of frame height) for a camera with this vertical field of view. */
export const focalFromVfov = (vfovDeg: number) => 0.5 / Math.tan(((vfovDeg / 2) * Math.PI) / 180);

export class Tracker {
  private tracks: Track[] = [];
  private nextId = 1;

  constructor(public focalFrac = focalFromVfov(45)) {}

  update(dets: (Detection & { label: string })[], now: number): Track[] {
    this.tracks = this.tracks.filter((t) => now - t.lastSeen < WINDOW_MS);
    const used = new Set<Track>();
    for (const d of dets) {
      let best: Track | null = null;
      let bestScore = 0;
      for (const t of this.tracks) {
        if (used.has(t) || t.label !== d.label) continue;
        const o = iou(t.box, d.box);
        const s = o >= IOU ? 1 + o : centreJump(t.box, d.box) <= CENTER_GATE ? 0.5 : 0;
        if (s > bestScore) {
          best = t;
          bestScore = s;
        }
      }
      if (!best) {
        best = {
          id: this.nextId++, thing: d.thing, label: d.label, box: d.box, score: d.score, lastSeen: now,
          history: [], distanceM: null, approachMps: 0, timeToContactS: null, approaching: false,
        };
        this.tracks.push(best);
      }
      used.add(best);
      best.box = d.box;
      best.score = d.score;
      best.lastSeen = now;
      best.history.push({ t: now, h: d.box.h, area: d.box.w * d.box.h });
      best.history = best.history.filter((p) => now - p.t <= HISTORY_MS);
      this.measure(best);
    }
    return this.tracks.filter((t) => t.lastSeen === now);
  }

  private measure(t: Track) {
    t.distanceM = estimateDistanceM(t.label, t.box, this.focalFrac);
    const h = t.history;
    const first = h[0];
    const last = h[h.length - 1];
    if (!first || !last || h.length < 4 || last.t - first.t < 300) {
      t.approachMps = 0;
      t.timeToContactS = null;
      t.approaching = false;
      return;
    }
    // Box growth rate: a box that grows fast is coming at you fast. Works even with no known size.
    const dt = (last.t - first.t) / 1000;
    const growth = (last.h - first.h) / first.h / dt; // fraction per second
    const d0 = estimateDistanceM(t.label, { ...t.box, h: first.h }, this.focalFrac);
    t.approachMps = d0 !== null && t.distanceM !== null ? (d0 - t.distanceM) / dt : 0;
    // Time to contact from growth alone (tau = h / dh/dt): no real size needed.
    t.timeToContactS = growth > 0.05 ? 1 / growth : null;
    t.approaching = growth > 0.25 && (t.timeToContactS ?? 99) < 4;
  }

  reset() {
    this.tracks = [];
  }
}
