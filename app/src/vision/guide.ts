// Turns what the laptop camera sees (tracks + nav.cpp's calls) into the hat's events, with short,
// clear walking instructions for the phone to speak. Dev-only demo layer: the rules for WHEN to
// warn and WHICH way are still nav.cpp's (ported in nav.ts); this only words them for walking and
// adds the distance estimate and a "path clear" once the way opens up.
import type { BrimEvent, ClearPathEvent, Direction, ObstacleKind, Proximity, SafetyAlertEvent } from "@schema";
import safetyJson from "../../../config/safety.json";
import { NAV, type ClearPathCall, type FrameDecision, type Thing } from "./nav";
import type { Track } from "./tracker";

export const SOURCE = "laptop-camera";
const ALERT_REPEAT_MS = 4000;
const APPROACH_REPEAT_MS = 5000;
const ALL_CLEAR_AFTER_MS = 1200;
const SEEN_EVERY_MS = 400;

const BRIM_THINGS = new Set<Thing>(["person", "chair", "table", "vehicle", "bicycle", "unknown"]);

/**
 * Same close/near/far rule as the hat (config/safety.json, box area fractions). `scale` comes from
 * the wearer's sensitivity: below 1 warns earlier (smaller boxes count as close), above 1 later.
 */
export function proximityOf(t: Pick<Track, "thing" | "box">, scale = 1): "close" | "near" | "far" {
  const a = t.box.w * t.box.h;
  const close = (t.thing === "person" ? safetyJson.person_close_area_frac : safetyJson.object_close_area_frac) * scale;
  return a >= close ? "close" : a >= safetyJson.near_area_frac * scale ? "near" : "far";
}

export interface GuideOptions {
  /** low = only when very close, high = warn earlier. */
  sensitivity: "low" | "normal" | "high";
  /** Obstacles and "step left/right". Off = only people and things coming at you. */
  hazards: boolean;
  /** Say where people are (never who). */
  people: boolean;
}
const SCALE = { low: 1.35, normal: 1, high: 0.7 } as const;
/** The walking corridor: middle band, lower half (nav.cpp). */
export function inCorridor(t: Pick<Track, "box">): boolean {
  const m = NAV.side_margin_frac;
  return t.box.y + t.box.h > 0.5 && t.box.x < 1 - m && t.box.x + t.box.w > m;
}
function sideOf(t: Pick<Track, "box">): Direction {
  const c = t.box.x + t.box.w / 2;
  const m = NAV.side_margin_frac;
  return c < m ? "left" : c > 1 - m ? "right" : "ahead";
}
const nameOf = (thing: Thing) => (thing === "unknown" ? "Something" : thing.charAt(0).toUpperCase() + thing.slice(1));

/** "very close", "1 metre", "1.5 metres". Rounded to half metres: it is an estimate. */
export function distanceWords(d: number | null): string {
  if (d === null || !Number.isFinite(d)) return "";
  if (d < 0.8) return "very close";
  const r = Math.round(d * 2) / 2;
  return `${r % 1 === 0 ? r.toFixed(0) : r.toFixed(1)} ${r === 1 ? "metre" : "metres"}`;
}

/** Clear walking words for one of nav.cpp's calls. */
export function walkingWords(call: ClearPathCall, because: Thing, distance: number | null): string {
  if (!call.clear) return "Stop. Path blocked.";
  if (call.direction === "ahead") return "Gap ahead. Keep going straight.";
  const step = `Step ${call.direction}.`;
  const name = nameOf(because);
  if (call.blocked.includes("ahead")) {
    const d = distanceWords(distance);
    return `${name} ahead${d ? `, ${d}` : ""}. ${step}`;
  }
  return `${name} on your ${call.direction === "left" ? "right" : "left"}. ${step}`;
}

export interface GuideInput {
  now: number;
  call: ClearPathCall | null;
  frame: FrameDecision;
  tracks: Track[];
  minConfidence: number;
}

export class Guide {
  private seq = 0;
  private opts: GuideOptions = { sensitivity: "normal", hazards: true, people: true };

  setOptions(o: Partial<GuideOptions>) {
    this.opts = { ...this.opts, ...o };
  }
  private lastAlert = new Map<string, number>();
  private lastSeen = new Map<number, number>();
  private needsAllClear = false;
  private clearSince: number | null = null;

  private base(now: number) {
    return { seq: ++this.seq, source: SOURCE, t_mono_ms: Math.round(now) };
  }

  reset() {
    this.lastAlert.clear();
    this.lastSeen.clear();
    this.needsAllClear = false;
    this.clearSince = null;
  }

  /** One frame in, zero or more events out (safety first, then the direction, then info). */
  update({ now, call: navCall, frame, tracks, minConfidence }: GuideInput): BrimEvent[] {
    let call = navCall;
    const out: BrimEvent[] = [];
    const scale = SCALE[this.opts.sensitivity];
    const sure = tracks.filter(
      (t) =>
        BRIM_THINGS.has(t.thing) &&
        t.score >= minConfidence &&
        (this.opts.hazards || t.thing === "person") &&
        (this.opts.people || t.thing !== "person"),
    );
    if (!this.opts.hazards) call = null; // no "step left / right" when hazards are off

    // 1. Safety: something close in the walking corridor.
    const close = sure
      .filter((t) => inCorridor(t) && proximityOf(t, scale) === "close")
      .sort((a, b) => b.box.h - a.box.h)[0];
    if (close) {
      const text = `Stop. ${nameOf(close.thing)} right in front of you.`;
      if (this.ok(text, now, ALERT_REPEAT_MS)) out.push(this.alert(now, close, text, "critical"));
    }
    // 2. Safety: something coming toward you fast.
    const coming = sure.filter((t) => t.approaching && t !== close).sort((a, b) => b.box.h - a.box.h)[0];
    if (coming) {
      const text = `${nameOf(coming.thing)} coming toward you.`;
      if (this.ok(`approach-${coming.id}`, now, APPROACH_REPEAT_MS)) out.push(this.alert(now, coming, text, "warning"));
    }

    // 3. Navigation: nav.cpp decided; we word it for walking, with the distance.
    if (call) {
      const blocker = sure
        .filter((t) => t.thing === frame.because && (inCorridor(t) || sideOf(t) !== "ahead"))
        .sort((a, b) => b.box.h - a.box.h)[0];
      const e: ClearPathEvent = {
        type: "clear_path",
        ...this.base(now),
        clear: call.clear,
        direction: call.direction,
        text: walkingWords(call, frame.because, blocker?.distanceM ?? null),
        blocked: call.blocked,
      };
      out.push(e);
      this.needsAllClear = true;
      this.clearSince = null;
    }

    // 4. "Path clear" once the way has stayed open for a moment after any instruction.
    if (frame.kind === "nocall" && !close) {
      this.clearSince ??= now;
      if (this.needsAllClear && now - this.clearSince >= ALL_CLEAR_AFTER_MS) {
        out.push({
          type: "clear_path",
          ...this.base(now),
          clear: true,
          direction: "ahead",
          text: "Path clear. Walk forward.",
          blocked: [],
        });
        this.needsAllClear = false;
      }
    } else if (!call) {
      this.clearSince = null;
    }
    if (close || coming) this.needsAllClear = true;

    // 5. Info for the screen: what is where (not spoken while walking; the phone shows it).
    for (const t of sure) {
      if (now - (this.lastSeen.get(t.id) ?? -Infinity) < SEEN_EVERY_MS) continue;
      this.lastSeen.set(t.id, now);
      const proximity: Proximity = proximityOf(t, scale);
      if (t.thing === "person") {
        out.push({ type: "person_seen", ...this.base(now), track_id: t.id, direction: sideOf(t), proximity, confidence: t.score });
      } else {
        const label: ObstacleKind = t.thing === "unknown" ? "obstacle" : (t.thing as ObstacleKind);
        out.push({
          type: "obstacle_seen",
          ...this.base(now),
          track_id: t.id,
          label,
          direction: sideOf(t),
          proximity,
          confidence: t.score,
          area_frac: t.box.w * t.box.h,
        });
      }
    }
    for (const id of this.lastSeen.keys()) if (!tracks.some((t) => t.id === id)) this.lastSeen.delete(id);
    return out;
  }

  private ok(key: string, now: number, every: number): boolean {
    if (now - (this.lastAlert.get(key) ?? -Infinity) < every) return false;
    this.lastAlert.set(key, now);
    return true;
  }

  private alert(now: number, t: Track, text: string, severity: "critical" | "warning"): SafetyAlertEvent {
    return {
      type: "safety_alert",
      ...this.base(now),
      kind: t.thing === "person" ? "person_close" : "object_close",
      severity,
      text,
      direction: sideOf(t),
      proximity: proximityOf(t, SCALE[this.opts.sensitivity]),
      confidence: t.score,
    };
  }
}
