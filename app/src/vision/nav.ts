// Line-by-line TypeScript port of device/src/nav/nav.cpp, so the laptop camera test gives exactly
// the calls the hat will give. If nav.cpp changes, change this file too (nav.test.ts mirrors
// device/tests/test_nav.cpp). Thresholds come from the same config/*.json files the hat reads.
import navJson from "../../../config/nav.json";
import safetyJson from "../../../config/safety.json";

export type Thing = "person" | "phone" | "chair" | "table" | "vehicle" | "bicycle" | "unknown";
export type Direction = "left" | "ahead" | "right" | "unknown";

/** x, y = top-left corner; all values are fractions (0–1) of the frame. */
export interface Box {
  x: number;
  y: number;
  w: number;
  h: number;
}
export interface Detection {
  thing: Thing;
  box: Box;
  score: number;
  trackId?: number;
}

export interface NavThresholds {
  side_margin_frac: number;
  clear_min_width_frac: number;
  clear_max_area_frac: number;
  decision_hold_ms: number;
  confirm_frames: number;
}
export const NAV: NavThresholds = navJson;
export const MIN_CONFIDENCE: number = safetyJson.min_confidence;
export const PATH_UNCLEAR = "Path unclear, stop";

const area = (b: Box) => b.w * b.h;
const cx = (b: Box) => b.x + b.w / 2;
const saneMargin = (m: number) => (m <= 0 || m >= 0.5 ? 0.33 : m);
const xOverlap = (b: Box, lo: number, hi: number) => Math.max(0, Math.min(b.x + b.w, hi) - Math.max(b.x, lo));

/** Width of [lo, hi] not covered by any of the boxes (union of their x-intervals). */
function freeWidth(blockers: Detection[], lo: number, hi: number): number {
  const iv = blockers
    .map((d) => [Math.max(d.box.x, lo), Math.min(d.box.x + d.box.w, hi)] as const)
    .filter(([a, b]) => b > a)
    .sort((p, q) => p[0] - q[0] || p[1] - q[1]);
  let covered = 0;
  let end = lo;
  for (const [a, b] of iv) {
    const start = Math.max(a, end);
    if (b > start) {
      covered += b - start;
      end = b;
    }
  }
  return hi - lo - covered;
}

function biggest(ds: Detection[]): Detection {
  return ds.reduce((best, d) => (area(d.box) > area(best.box) ? d : best));
}

export interface FrameDecision {
  kind: "nocall" | "go" | "stop";
  direction: Direction;
  blocked: Direction[];
  because: Thing;
}
const noCall = (): FrameDecision => ({ kind: "nocall", direction: "unknown", blocked: [], because: "unknown" });
const sameCall = (a: FrameDecision, b: FrameDecision) => a.kind === b.kind && a.direction === b.direction;

/** Pure per-frame geometry. Phones and boxes below min_confidence are never obstacles. */
export function decideFrame(dets: Detection[], t: NavThresholds = NAV, minConfidence = MIN_CONFIDENCE): FrameDecision {
  const out = noCall();
  const m = saneMargin(t.side_margin_frac);

  // Only things big enough to be close count. Closeness is box area: no distance sensor.
  const blockers = dets.filter(
    (d) => d.thing !== "phone" && d.score >= minConfidence && area(d.box) > t.clear_max_area_frac,
  );
  if (blockers.length === 0) return out;

  // Ahead: anything overlapping the corridor (middle band, lower half: the floor in front).
  const inCorridor: Detection[] = [];
  const inLeft: Detection[] = [];
  const inRight: Detection[] = [];
  for (const d of blockers) {
    const lowerHalf = d.box.y + d.box.h > 0.5;
    if (lowerHalf && xOverlap(d.box, m, 1 - m) > 0) inCorridor.push(d);
    if (xOverlap(d.box, 0, m) > 0) inLeft.push(d);
    if (xOverlap(d.box, 1 - m, 1) > 0) inRight.push(d);
  }
  const aheadBlocked = inCorridor.length > 0;
  const leftClear = freeWidth(inLeft, 0, m) >= t.clear_min_width_frac;
  const rightClear = freeWidth(inRight, 1 - m, 1) >= t.clear_min_width_frac;

  if (aheadBlocked) {
    out.blocked.push("ahead");
    out.because = biggest(inCorridor).thing;
    if (!leftClear && !rightClear) {
      return { kind: "stop", direction: "unknown", blocked: ["left", "ahead", "right"], because: out.because };
    }
    out.kind = "go";
    if (leftClear && rightClear) {
      // Both sides free: step away from where the obstacle sits (right on an exact tie).
      out.direction = cx(biggest(inCorridor).box) > 0.5 ? "left" : "right";
    } else {
      out.direction = leftClear ? "left" : "right";
      out.blocked.push(leftClear ? "right" : "left");
    }
    return out;
  }

  if (leftClear && rightClear) return out; // nothing near our path: no call
  out.kind = "go";
  if (!leftClear && !rightClear) {
    // Obstacles on both sides with a gap in the middle: keep going straight.
    return { kind: "go", direction: "ahead", blocked: ["left", "right"], because: biggest(blockers).thing };
  }
  out.direction = leftClear ? "left" : "right";
  out.blocked.push(leftClear ? "right" : "left");
  out.because = biggest(leftClear ? inRight : inLeft).thing;
  return out;
}

/** The words for a decision, e.g. "Chair ahead, step left", "Path unclear, stop". */
export function textFor(d: FrameDecision): string {
  if (d.kind === "stop" || d.kind === "nocall") return PATH_UNCLEAR;
  if (d.direction === "ahead") return "Clear path ahead";
  const raw = d.because === "unknown" ? "something" : d.because;
  const name = raw.charAt(0).toUpperCase() + raw.slice(1);
  const step = d.direction === "left" ? "step left" : "step right";
  if (d.blocked.includes("ahead")) return `${name} ahead, ${step}`;
  const side = d.direction === "left" ? "on your right" : "on your left";
  return `${name} ${side}, ${step}`;
}

export interface ClearPathCall {
  clear: boolean;
  direction: Direction;
  text: string;
  blocked: Direction[];
}

/** Smoothing over frames: confirmation, hold, and "unsure means stop". Same as nav::Navigator. */
export class Navigator {
  private candidate = noCall();
  private streak = 0;
  private busyFrames = 0;
  private hasCurrent = false;
  private current = noCall();
  private currentSince = 0;

  constructor(
    private t: NavThresholds = NAV,
    private minConf = MIN_CONFIDENCE,
  ) {}

  /** The latest per-frame decision (for drawing), whether or not it was announced. */
  get lastFrame(): FrameDecision {
    return this.candidate;
  }

  reset() {
    this.candidate = noCall();
    this.streak = 0;
    this.busyFrames = 0;
    this.hasCurrent = false;
    this.current = noCall();
    this.currentSince = 0;
  }

  private announce(d: FrameDecision, nowMs: number): ClearPathCall {
    this.current = d;
    this.hasCurrent = true;
    this.currentSince = nowMs;
    const clear = d.kind === "go";
    return { clear, direction: clear ? d.direction : "unknown", text: textFor(d), blocked: d.blocked };
  }

  update(dets: Detection[], nowMs: number): ClearPathCall | null {
    const confirm = Math.min(3, Math.max(2, this.t.confirm_frames));
    const d = decideFrame(dets, this.t, this.minConf);
    this.streak = this.streak > 0 && sameCall(d, this.candidate) ? this.streak + 1 : 1;
    this.candidate = d;
    const confirmed = this.streak >= confirm;

    // Obstacles are there but frames keep disagreeing: we are not sure, so we say stop.
    this.busyFrames = d.kind !== "nocall" && !confirmed ? this.busyFrames + 1 : 0;
    if (this.busyFrames >= 3 * confirm) {
      this.busyFrames = 0;
      if (this.hasCurrent && this.current.kind === "stop") return null;
      return this.announce({ kind: "stop", direction: "unknown", blocked: ["left", "ahead", "right"], because: "unknown" }, nowMs);
    }

    if (!confirmed) return null;
    if (d.kind === "nocall") {
      this.hasCurrent = false;
      return null;
    }
    if (this.hasCurrent && sameCall(this.current, d)) return null;
    if (this.hasCurrent && nowMs - this.currentSince < this.t.decision_hold_ms && d.kind !== "stop") return null;
    return this.announce(d, nowMs);
  }
}
