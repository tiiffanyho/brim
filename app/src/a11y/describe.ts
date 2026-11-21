// Turns events into short sentences for screen readers. Pure functions (unit-tested).
// Phrases are kept short: the wearer hears them while walking.
import type { BrimEvent, Direction, ObstacleKind, Proximity } from "@schema";
import type { Tier } from "./speech";

export type Politeness = "polite" | "assertive";
export interface Announcement {
  text: string;
  /** assertive only for safety_alert (it interrupts); polite for everything else. */
  politeness: Politeness;
  /** When it is said: see speech.ts. */
  tier: Tier;
  /** Same key within the dedupe window is not re-announced (never applied to alert or nav). */
  key: string;
}

/** Longer device messages are replaced by a short generic phrase. */
const MAX_FAULT_WORDS = 6;

export function directionWords(d: Direction): string {
  switch (d) {
    case "left":
      return "on your left";
    case "right":
      return "on your right";
    case "ahead":
      return "ahead";
    default:
      return "";
  }
}

/** How close, in words. "far" and "unknown" say nothing rather than guess. */
export function proximityWords(p: Proximity): string {
  switch (p) {
    case "close":
      return "close by";
    case "near":
      return "nearby";
    default:
      return "";
  }
}

const OBSTACLE_WORDS: Record<ObstacleKind, string> = {
  person: "Person",
  chair: "Chair",
  table: "Table",
  door: "Door",
  pole: "Pole",
  stairs: "Stairs",
  vehicle: "Vehicle",
  bicycle: "Bicycle",
  obstacle: "Something", // the detector was sure there is an obstacle but not what it is
};

export function obstacleWords(k: ObstacleKind): string {
  return OBSTACLE_WORDS[k] ?? "Something";
}

/** "Person on your left, close by." — joins the parts we actually have. */
function sentence(subject: string, d: Direction, p: Proximity): string {
  const parts = [directionWords(d), proximityWords(p)].filter(Boolean);
  return `${subject}${parts.length ? ` ${parts.join(", ")}` : ""}.`;
}

/** Which events the phone says, and how urgently. */
export function describeEvent(e: BrimEvent): Announcement | null {
  switch (e.type) {
    case "safety_alert":
      return { text: `${e.text}.`, politeness: "assertive", tier: "alert", key: `safety:${e.kind}:${e.text}` };
    case "clear_path":
      // Said as soon as it arrives (right after the warning that made it necessary), never deduped.
      return { text: `${e.text}.`, politeness: "polite", tier: "nav", key: `path:${e.clear ? e.direction : "unclear"}` };
    case "person_seen":
      return { text: sentence("Person", e.direction, e.proximity), politeness: "polite", tier: "info", key: `person:${e.track_id}` };
    case "obstacle_seen":
      return {
        text: sentence(obstacleWords(e.label), e.direction, e.proximity),
        politeness: "polite",
        tier: "info",
        key: `obstacle:${e.label}:${e.direction}`,
      };
    case "fault":
      if (e.action === "restarting") return { text: "Brim is restarting.", politeness: "polite", tier: "status", key: "fault:restarting" };
      if (e.action === "recovered") return { text: "Brim is working again.", politeness: "polite", tier: "status", key: "fault:recovered" };
      if (e.action === "degraded") {
        const msg = e.message.trim().replace(/[.!]+$/, "");
        const short = msg && msg.split(/\s+/).length <= MAX_FAULT_WORDS ? msg : "Brim has a problem";
        return { text: `${short}.`, politeness: "polite", tier: "status", key: `fault:${e.component}` };
      }
      return null;
    case "health": // far too chatty to say
      return null;
  }
}

/** Suppresses repeats of the same key within windowMs (critical safety is never suppressed). */
export class Deduper {
  private last = new Map<string, number>();
  constructor(private readonly windowMs = 30_000) {}
  shouldAnnounce(a: Announcement, nowMs: number, neverSuppress = false): boolean {
    const prev = this.last.get(a.key);
    if (!neverSuppress && prev !== undefined && nowMs - prev < this.windowMs) return false;
    this.last.set(a.key, nowMs);
    return true;
  }
}

export function relativeTime(iso: string, now = Date.now()): string {
  const t = Date.parse(iso);
  if (Number.isNaN(t)) return "";
  const s = Math.max(0, Math.round((now - t) / 1000));
  if (s < 10) return "just now";
  if (s < 60) return `${s} seconds ago`;
  const m = Math.round(s / 60);
  if (m < 60) return m === 1 ? "1 minute ago" : `${m} minutes ago`;
  const h = Math.round(m / 60);
  if (h < 24) return h === 1 ? "1 hour ago" : `${h} hours ago`;
  return new Date(t).toLocaleDateString();
}
