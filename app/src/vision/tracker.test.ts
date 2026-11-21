import { describe, expect, it } from "vitest";
import { estimateDistanceM, focalFromVfov, Tracker } from "./tracker";

const person = (h: number, x = 0.4) => ({
  thing: "person" as const,
  label: "person",
  score: 0.9,
  box: { x, y: 0.1, w: h * 0.4, h },
});

describe("distance", () => {
  it("halves when the box doubles in height", () => {
    const f = focalFromVfov(45);
    const a = estimateDistanceM("person", { x: 0, y: 0, w: 0.1, h: 0.3 }, f)!;
    const b = estimateDistanceM("person", { x: 0, y: 0, w: 0.2, h: 0.6 }, f)!;
    expect(a / b).toBeCloseTo(2, 5);
  });
  it("is unknown for things with no typical size", () => {
    expect(estimateDistanceM("kite", { x: 0, y: 0, w: 0.1, h: 0.3 }, 1)).toBeNull();
  });
});

describe("tracker", () => {
  it("keeps the same id while a box moves a little", () => {
    const tr = new Tracker();
    const a = tr.update([person(0.3, 0.4)], 0)[0]!;
    const b = tr.update([person(0.31, 0.42)], 66)[0]!;
    expect(b.id).toBe(a.id);
  });

  it("gives a new id after the window runs out", () => {
    const tr = new Tracker();
    const a = tr.update([person(0.3)], 0)[0]!;
    const b = tr.update([person(0.3)], 5000)[0]!;
    expect(b.id).not.toBe(a.id);
  });

  it("flags something growing fast as approaching, with a time to contact", () => {
    const tr = new Tracker();
    let t = 0;
    let last;
    for (let h = 0.2; h <= 0.5; h += 0.03, t += 66) last = tr.update([person(h)], t)[0]!;
    expect(last!.approaching).toBe(true);
    expect(last!.approachMps).toBeGreaterThan(0);
    expect(last!.timeToContactS).not.toBeNull();
  });

  it("does not flag a box that stays the same size", () => {
    const tr = new Tracker();
    let last;
    for (let t = 0; t < 1200; t += 66) last = tr.update([person(0.3)], t)[0]!;
    expect(last!.approaching).toBe(false);
  });
});
