// Turns events into short sentences for screen readers. Pure functions (unit-tested).
import type { BrimEvent, Direction, ObstacleKind, Proximity } from "@schema";

export type Politeness = "polite" | "assertive";
export interface Announcement {
  text: string;
  politeness: Politeness;
  /** Same key within the dedupe window is not re-announced. */
  key: string;
}

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

/** Which events should be spoken by the phone's screen reader, and how urgently. */
export function describeEvent(e: BrimEvent): Announcement | null {
  switch (e.type) {
    case "safety_alert":
      return {
        text: `${e.severity === "critical" ? "Warning: " : ""}${e.text}.`,
        politeness: e.severity === "info" ? "polite" : "assertive",
        key: `safety:${e.kind}:${e.text}`,
      };
    case "clear_path":
      // Navigation advice: said right after the warning that made it necessary, so it must not be
      // suppressed into silence — but "Path unclear, stop" is the urgent one.
      return {
        text: `${e.text}.`,
        politeness: e.clear ? "polite" : "assertive",
        key: `path:${e.clear ? e.direction : "unclear"}`,
      };
    case "person_seen":
      return { text: sentence("Person", e.direction, e.proximity), politeness: "polite", key: `person:${e.track_id}` };
    case "obstacle_seen":
      return {
        text: sentence(obstacleWords(e.label), e.direction, e.proximity),
        politeness: "polite",
        key: `obstacle:${e.label}:${e.direction}`,
      };
    case "fault":
      if (e.action === "restarting") return { text: "Hat is restarting its camera system.", politeness: "polite", key: "fault:restarting" };
      if (e.action === "recovered") return { text: "Hat is working again.", politeness: "polite", key: "fault:recovered" };
      if (e.action === "degraded") return { text: `Hat problem: ${e.message}.`, politeness: "assertive", key: `fault:${e.component}` };
      return null;
    case "health": // far too chatty for a live region
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
