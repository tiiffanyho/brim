import { describe, expect, it } from "vitest";
import type { BrimEvent } from "@schema";
import { Deduper, describeEvent, obstacleWords, relativeTime } from "./describe";

const base = { seq: 1, source: "brim", t_mono_ms: 1 };

describe("describeEvent", () => {
  it("announces safety alerts assertively, critical ones with a warning word", () => {
    const e: BrimEvent = { ...base, type: "safety_alert", kind: "person_close", severity: "critical", text: "Person close ahead", direction: "ahead" };
    expect(describeEvent(e)?.politeness).toBe("assertive");
    expect(describeEvent(e)?.text).toBe("Warning: Person close ahead.");
    expect(describeEvent({ ...e, severity: "info" })?.politeness).toBe("polite");
  });

  it("announces a phone pointed at the wearer", () => {
    const a = describeEvent({ ...base, type: "safety_alert", kind: "phone_raised", severity: "warning", text: "A phone is pointed at you", direction: "ahead" });
    expect(a).toEqual({ text: "A phone is pointed at you.", politeness: "assertive", key: "safety:phone_raised:A phone is pointed at you" });
  });

  it("announces the safest direction, and 'stop' assertively when there is none", () => {
    expect(describeEvent({ ...base, type: "clear_path", clear: true, direction: "right", text: "Chair ahead, step right" })).toEqual({
      text: "Chair ahead, step right.",
      politeness: "polite",
      key: "path:right",
    });
    const stop = describeEvent({ ...base, type: "clear_path", clear: false, direction: "unknown", text: "Path unclear, stop" });
    expect(stop?.politeness).toBe("assertive");
    expect(stop?.key).toBe("path:unclear");
  });

  it("says where a person is without saying who they are", () => {
    const a = describeEvent({ ...base, type: "person_seen", track_id: 4, direction: "left", proximity: "close", confidence: 0.9 });
    expect(a).toEqual({ text: "Person on your left, close by.", politeness: "polite", key: "person:4" });
    expect(JSON.stringify(a)).not.toMatch(/name/i);
  });

  it("names an obstacle and its side, and stays vague when the label is vague", () => {
    expect(describeEvent({ ...base, type: "obstacle_seen", label: "chair", direction: "ahead", proximity: "near", confidence: 0.8 })?.text).toBe(
      "Chair ahead, nearby.",
    );
    expect(describeEvent({ ...base, type: "obstacle_seen", label: "obstacle", direction: "right", proximity: "close", confidence: 0.7 })?.text).toBe(
      "Something on your right, close by.",
    );
    expect(obstacleWords("stairs")).toBe("Stairs");
  });

  it("stays quiet about things that are far away or unknown", () => {
    expect(describeEvent({ ...base, type: "person_seen", track_id: 1, direction: "unknown", proximity: "far", confidence: 0.9 })?.text).toBe(
      "Person.",
    );
  });

  it("never announces health", () => {
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
    const a = { text: "Person on your left, close by.", politeness: "polite" as const, key: "person:1" };
    expect(d.shouldAnnounce(a, 0)).toBe(true);
    expect(d.shouldAnnounce(a, 10_000)).toBe(false);
    expect(d.shouldAnnounce(a, 40_001)).toBe(true);
    expect(d.shouldAnnounce(a, 40_002, true)).toBe(true); // urgent bypass
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
