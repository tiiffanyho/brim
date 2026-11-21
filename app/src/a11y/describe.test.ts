import { describe, expect, it } from "vitest";
import type { BrimEvent } from "@schema";
import { Deduper, describeEvent, withName, obstacleWords, relativeTime } from "./describe";

const base = { seq: 1, source: "brim", t_mono_ms: 1 };

describe("describeEvent", () => {
  it("says every safety alert assertively, as a short phrase", () => {
    const e: BrimEvent = { ...base, type: "safety_alert", kind: "person_close", severity: "critical", text: "Person ahead, move left", direction: "ahead" };
    expect(describeEvent(e)?.politeness).toBe("assertive");
    expect(describeEvent(e)?.tier).toBe("alert");
    expect(describeEvent(e)?.text).toBe("Person ahead, move left.");
    expect(describeEvent({ ...e, severity: "info" })?.politeness).toBe("assertive");
  });

  it("announces a phone pointed at the wearer", () => {
    const a = describeEvent({ ...base, type: "safety_alert", kind: "phone_raised", severity: "warning", text: "A phone is pointed at you", direction: "ahead" });
    expect(a).toEqual({
      text: "A phone is pointed at you.",
      politeness: "assertive",
      tier: "alert",
      key: "safety:phone_raised:A phone is pointed at you",
    });
  });

  it("says the safest direction, or 'stop' when there is none, in the polite region at nav priority", () => {
    expect(describeEvent({ ...base, type: "clear_path", clear: true, direction: "right", text: "Chair ahead, move right" })).toEqual({
      text: "Chair ahead, move right.",
      politeness: "polite",
      tier: "nav",
      key: "path:right",
    });
    const stop = describeEvent({ ...base, type: "clear_path", clear: false, direction: "unknown", text: "Path unclear, stop" });
    expect(stop?.text).toBe("Path unclear, stop.");
    expect(stop?.politeness).toBe("polite");
    expect(stop?.tier).toBe("nav");
    expect(stop?.key).toBe("path:unclear");
  });

  it("names an enrolled person only, and only in 'Person ahead' phrases", () => {
    const alert = { ...base, type: "safety_alert" as const, kind: "person_close" as const, severity: "critical" as const, direction: "ahead" as const };
    expect(describeEvent({ ...alert, text: "Person ahead, move left", name: "Tiffany" })?.text).toBe("Tiffany ahead, move left.");
    expect(describeEvent({ ...alert, text: "Person ahead, stop", name: "Tiffany" })?.text).toBe("Tiffany ahead, stop.");
    expect(describeEvent({ ...alert, text: "Person ahead, move left" })?.text).toBe("Person ahead, move left.");
    expect(describeEvent({ ...alert, kind: "object_close", text: "Chair ahead, move left", name: "Tiffany" })?.text).toBe("Chair ahead, move left.");
    expect(describeEvent({ ...alert, text: "Person ahead, move left", name: "<b>x</b>" })?.text).toBe("Person ahead, move left.");
    const path = { ...base, type: "clear_path" as const, clear: true, direction: "right" as const, text: "Person ahead, move right" };
    expect(describeEvent({ ...path, name: "Tiffany" })?.text).toBe("Tiffany ahead, move right.");
    expect(withName("Person ahead, move right", "Tiffany")).toBe("Tiffany ahead, move right");
  });

  it("says where a person is without saying who they are", () => {
    const a = describeEvent({ ...base, type: "person_seen", track_id: 4, direction: "left", proximity: "close", confidence: 0.9 });
    expect(a).toEqual({ text: "Person on your left, close by.", politeness: "polite", tier: "info", key: "person:4" });
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

  it("says faults in a few words", () => {
    expect(describeEvent({ ...base, type: "fault", component: "brim", action: "restarting", message: "heartbeat missed for 1137 ms" })?.text).toBe(
      "Brim is restarting.",
    );
    expect(describeEvent({ ...base, type: "fault", component: "brim", action: "recovered", message: "x" })?.text).toBe("Brim is working again.");
    const blind = describeEvent({ ...base, type: "fault", component: "detect", action: "degraded", message: "Brim cannot see. Stop" });
    expect(blind).toMatchObject({ text: "Brim cannot see. Stop.", politeness: "polite", tier: "status" });
    const long = describeEvent({
      ...base,
      type: "fault",
      component: "camera",
      action: "degraded",
      message: "frame rate dropped below the configured minimum for a while",
    });
    expect(long?.text).toBe("Brim has a problem.");
  });
});

describe("Deduper", () => {
  it("suppresses the same key within the window", () => {
    const d = new Deduper(30_000);
    const a = { text: "Person on your left, close by.", politeness: "polite" as const, tier: "info" as const, key: "person:1" };
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
