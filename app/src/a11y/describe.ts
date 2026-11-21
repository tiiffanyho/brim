// Turns events into short sentences for screen readers. Pure functions (unit-tested).
import type { BrimEvent, Direction } from "@schema";

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

/** Which events should be spoken by the phone's screen reader, and how urgently. */
export function describeEvent(e: BrimEvent): Announcement | null {
  switch (e.type) {
    case "person_recognized": {
      const where = directionWords(e.direction);
      return { text: `${e.display_name} is here${where ? `, ${where}` : ""}.`, politeness: "polite", key: `person:${e.person_id}` };
    }
    case "safety_alert":
      return {
        text: `${e.severity === "critical" ? "Warning: " : ""}${e.text}.`,
        politeness: e.severity === "info" ? "polite" : "assertive",
        key: `safety:${e.kind}:${e.text}`,
      };
    case "fault":
      if (e.action === "restarting") return { text: "Hat is restarting its camera system.", politeness: "polite", key: "fault:restarting" };
      if (e.action === "recovered") return { text: "Hat is working again.", politeness: "polite", key: "fault:recovered" };
      if (e.action === "degraded") return { text: `Hat problem: ${e.message}.`, politeness: "assertive", key: `fault:${e.component}` };
      return null;
    case "cue":
      return { text: `${e.text}.`, politeness: "polite", key: `cue:${e.cue}:${e.person_id ?? e.anon_id ?? ""}` };
    case "unknown_person": // strangers are not announced on the phone (privacy + noise)
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
