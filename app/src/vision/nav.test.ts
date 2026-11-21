// Mirrors device/tests/test_nav.cpp so the browser port can't drift from the hat.
import { describe, expect, it } from "vitest";
import { NAV, Navigator, type ClearPathCall, type Detection, type Thing } from "./nav";

const det = (thing: Thing, x: number, y: number, w: number, h: number, score = 0.9): Detection => ({
  thing,
  box: { x, y, w, h },
  score,
});
const chairLeft = () => det("chair", 0.05, 0.45, 0.22, 0.45);
const chairCentre = () => det("chair", 0.4, 0.5, 0.2, 0.45);
const chairRight = () => det("chair", 0.73, 0.45, 0.22, 0.45);

function feed(n: Navigator, dets: Detection[], frames: number, clock: { t: number }): ClearPathCall[] {
  const out: ClearPathCall[] = [];
  for (let i = 0; i < frames; i++, clock.t += 66) {
    const e = n.update(dets, clock.t);
    if (e) out.push(e);
  }
  return out;
}

describe("nav (port of nav.cpp)", () => {
  it("a chair on the left gives 'step right'", () => {
    const c = feed(new Navigator(), [chairLeft()], 10, { t: 0 });
    expect(c).toHaveLength(1);
    expect(c[0]).toMatchObject({ clear: true, direction: "right", text: "Chair on your left, step right" });
    expect(c[0]!.blocked).toContain("left");
  });

  it("a chair on the right gives 'step left'", () => {
    const c = feed(new Navigator(), [chairRight()], 10, { t: 0 });
    expect(c[0]).toMatchObject({ direction: "left", text: "Chair on your right, step left" });
  });

  it("a chair ahead with one clear side gives that side", () => {
    const c = feed(new Navigator(), [chairCentre(), det("table", 0.7, 0.5, 0.25, 0.4)], 10, { t: 0 });
    expect(c[0]).toMatchObject({ clear: true, direction: "left", text: "Chair ahead, step left" });
    expect(c[0]!.blocked).toEqual(expect.arrayContaining(["ahead", "right"]));
    const c2 = feed(new Navigator(), [chairCentre(), det("table", 0.05, 0.5, 0.25, 0.4)], 10, { t: 0 });
    expect(c2[0]).toMatchObject({ direction: "right", text: "Chair ahead, step right" });
  });

  it("a chair ahead with both sides clear steps away from where it stands", () => {
    const c = feed(new Navigator(), [det("chair", 0.45, 0.5, 0.2, 0.45)], 10, { t: 0 });
    expect(c[0]!.direction).toBe("left");
  });

  it("a chair in all three bands gives 'Path unclear, stop'", () => {
    const c = feed(new Navigator(), [chairLeft(), chairCentre(), chairRight()], 10, { t: 0 });
    expect(c[0]).toMatchObject({ clear: false, direction: "unknown", text: "Path unclear, stop" });
    expect(c[0]!.blocked).toEqual(["left", "ahead", "right"]);
    const c2 = feed(new Navigator(), [det("table", 0, 0.4, 1, 0.5)], 10, { t: 0 });
    expect(c2[0]!.text).toBe("Path unclear, stop");
  });

  it("a tiny far chair gives no call", () => {
    expect(feed(new Navigator(), [det("chair", 0.45, 0.6, 0.1, 0.15)], 30, { t: 0 })).toHaveLength(0);
  });

  it("a one-frame blip gives no call", () => {
    const n = new Navigator();
    const t = { t: 0 };
    expect(feed(n, [], 5, t)).toHaveLength(0);
    expect(feed(n, [chairLeft(), chairCentre(), chairRight()], 1, t)).toHaveLength(0);
    expect(feed(n, [], 10, t)).toHaveLength(0);
    expect(feed(n, [chairLeft()], 1, t)).toHaveLength(0);
    expect(feed(n, [], 10, t)).toHaveLength(0);
  });

  it("a call needs confirm_frames matching frames", () => {
    const n = new Navigator();
    const t = { t: 0 };
    expect(feed(n, [chairLeft()], NAV.confirm_frames - 1, t)).toHaveLength(0);
    expect(feed(n, [chairLeft()], 1, t)).toHaveLength(1);
  });

  it("a decision is held for decision_hold_ms; stop is never held back", () => {
    const n = new Navigator();
    const t = { t: 0 };
    expect(feed(n, [chairLeft()], 5, t)).toHaveLength(1);
    const saidAt = t.t - 5 * 66 + (NAV.confirm_frames - 1) * 66;
    const held: ClearPathCall[] = [];
    while (t.t - saidAt < NAV.decision_hold_ms - 66) held.push(...feed(n, [chairRight()], 1, t));
    expect(held).toHaveLength(0);
    const later = feed(n, [chairRight()], 5, t);
    expect(later).toHaveLength(1);
    expect(later[0]!.direction).toBe("left");
    const stop = feed(n, [chairLeft(), chairCentre(), chairRight()], NAV.confirm_frames, t);
    expect(stop).toHaveLength(1);
    expect(stop[0]!.text).toBe("Path unclear, stop");
  });

  it("frames that keep disagreeing mean stop, never a guess", () => {
    const n = new Navigator();
    const t = { t: 0 };
    const calls: ClearPathCall[] = [];
    for (let i = 0; i < 20; i++) calls.push(...feed(n, [i % 2 ? chairLeft() : chairRight()], 1, t));
    expect(calls.length).toBeGreaterThan(0);
    for (const e of calls) expect(e).toMatchObject({ clear: false, text: "Path unclear, stop" });
  });

  it("phones and unsure boxes are never obstacles; unknown things are 'something'", () => {
    const n = new Navigator();
    const t = { t: 0 };
    expect(feed(n, [det("phone", 0.35, 0.5, 0.3, 0.45)], 10, t)).toHaveLength(0);
    expect(feed(n, [det("chair", 0.35, 0.5, 0.3, 0.45, 0.4)], 10, t)).toHaveLength(0);
    const c = feed(n, [det("unknown", 0.05, 0.45, 0.22, 0.45)], 10, t);
    expect(c).toHaveLength(1);
    expect(c[0]!.text).toBe("Something on your left, step right");
  });

  it("obstacles on both sides with a gap in the middle: clear path ahead", () => {
    const c = feed(new Navigator(), [chairLeft(), chairRight()], 10, { t: 0 });
    expect(c[0]).toMatchObject({ clear: true, direction: "ahead", text: "Clear path ahead" });
  });
});
