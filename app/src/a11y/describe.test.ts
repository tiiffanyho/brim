import { describe, expect, it } from "vitest";
import type { BrimEvent } from "@schema";
import { Deduper, describeEvent, relativeTime } from "./describe";

const base = { seq: 1, source: "brim", t_mono_ms: 1 };

describe("describeEvent", () => {
  it("announces recognized people politely with direction", () => {
    const a = describeEvent({ ...base, type: "person_recognized", person_id: 1, display_name: "Sam", confidence: 0.9, direction: "left" });
    expect(a).toEqual({ text: "Sam is here, on your left.", politeness: "polite", key: "person:1" });
  });

  it("announces critical/warning safety alerts assertively", () => {
    const e: BrimEvent = { ...base, type: "safety_alert", kind: "obstacle_head_height", severity: "critical", text: "Low branch ahead", direction: "ahead" };
    expect(describeEvent(e)?.politeness).toBe("assertive");
    expect(describeEvent(e)?.text).toBe("Warning: Low branch ahead.");
    expect(describeEvent({ ...e, severity: "info" })?.politeness).toBe("polite");
  });

  it("never announces strangers or health", () => {
    expect(describeEvent({ ...base, type: "unknown_person", anon_id: "stranger-1", direction: "ahead" })).toBeNull();
    expect(
      describeEvent({ ...base, type: "health", uptime_s: 1, fps: 15, cpu_pct: 1, pipeline_latency_ms: 1, speak_queue_len: 0, components: [] }),
    ).toBeNull();
  });

  it("announces restarts and recovery", () => {
    expect(describeEvent({ ...base, type: "fault", component: "brim", action: "restarting", message: "x" })?.text).toMatch(/restarting/);
    expect(describeEvent({ ...base, type: "fault", component: "brim", action: "recovered", message: "x" })?.text).toMatch(/working again/);
  });
});

describe("Deduper", () => {
  it("suppresses the same key within the window", () => {
    const d = new Deduper(30_000);
    const a = { text: "Sam is here.", politeness: "polite" as const, key: "person:1" };
    expect(d.shouldAnnounce(a, 0)).toBe(true);
    expect(d.shouldAnnounce(a, 10_000)).toBe(false);
    expect(d.shouldAnnounce(a, 40_001)).toBe(true);
    expect(d.shouldAnnounce(a, 40_002, true)).toBe(true); // critical bypass
  });
});

describe("relativeTime", () => {
  it("formats", () => {
    const now = Date.parse("2026-01-01T00:10:00Z");
    expect(relativeTime("2026-01-01T00:09:58Z", now)).toBe("just now");
    expect(relativeTime("2026-01-01T00:09:00Z", now)).toBe("1 minute ago");
    expect(relativeTime("2026-01-01T00:00:00Z", now)).toBe("10 minutes ago");
  });
});
