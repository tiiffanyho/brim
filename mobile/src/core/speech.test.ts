import { describe, expect, it } from "vitest";
import type { Politeness } from "./describe";
import { HealthWatch, SpeechScheduler } from "./speech";

function setup() {
  let t = 0;
  const said: { text: string; p: Politeness; at: number }[] = [];
  const s = new SpeechScheduler((text, p) => said.push({ text, p, at: t }), { now: () => t, msPerChar: 50, minSpeakMs: 800 });
  const advance = (ms: number) => {
    for (let i = 0; i < ms; i += 100) {
      t += 100;
      s.pump();
    }
  };
  return { s, said, advance, at: (ms: number) => (t = ms) };
}

describe("SpeechScheduler", () => {
  it("says a safety alert at once, in the assertive region, even mid-sentence", () => {
    const { s, said } = setup();
    s.push("Person on your left, close by.", "info");
    s.push("Person close ahead.", "alert");
    expect(said.map((x) => [x.text, x.p])).toEqual([
      ["Person on your left, close by.", "polite"],
      ["Person close ahead.", "assertive"],
    ]);
  });

  it("says a direction as soon as it arrives, but never on top of the warning before it", () => {
    const { s, said, advance } = setup();
    s.push("Chair close ahead.", "alert"); // 18 chars * 50 = 900 ms
    s.push("Chair ahead, step left.", "nav");
    expect(said).toHaveLength(1);
    advance(1000);
    expect(said.map((x) => x.text)).toEqual(["Chair close ahead.", "Chair ahead, step left."]);
    expect(said[1]?.p).toBe("polite");
    expect(said[1]?.at).toBeGreaterThanOrEqual(900);
  });

  it("a direction does not wait for an info phrase", () => {
    const { s, said } = setup();
    s.push("Person on your left.", "info");
    s.push("Path unclear, stop.", "nav");
    expect(said.map((x) => x.text)).toEqual(["Person on your left.", "Path unclear, stop."]);
  });

  it("only the latest direction is said when several wait", () => {
    const { s, said, advance } = setup();
    s.push("Person close ahead.", "alert");
    s.push("Step left.", "nav");
    s.push("Path unclear, stop.", "nav");
    advance(2000);
    expect(said.map((x) => x.text)).toEqual(["Person close ahead.", "Path unclear, stop."]);
  });

  it("says the rest only when nothing else is being said", () => {
    const { s, said, advance } = setup();
    s.push("Chair ahead, step right.", "nav"); // 24 * 50 = 1200 ms
    s.push("Person on your right.", "info");
    expect(said).toHaveLength(1);
    advance(1100);
    expect(said).toHaveLength(1);
    advance(200);
    expect(said.map((x) => x.text)).toEqual(["Chair ahead, step right.", "Person on your right."]);
  });

  it("drops info that waited more than 3 s; status waits as long as it takes", () => {
    const { s, said, advance } = setup();
    s.push("This is a very long safety warning that takes a while to read out.", "alert"); // ~3.4 s
    s.push("Chair on your left.", "info");
    s.push("Connection lost.", "status");
    advance(5000);
    expect(said.map((x) => x.text)).toEqual(["This is a very long safety warning that takes a while to read out.", "Connection lost."]);
  });

  it("status goes before info", () => {
    const { s, said, advance } = setup();
    s.push("Clear path ahead.", "nav");
    s.push("Person on your left.", "info");
    s.push("Brim is restarting.", "status");
    advance(3000);
    expect(said.map((x) => x.text)).toEqual(["Clear path ahead.", "Brim is restarting.", "Person on your left."]);
  });
});

describe("HealthWatch", () => {
  it("says nothing before the first health event", () => {
    const w = new HealthWatch(3000);
    expect(w.check(10_000)).toBeNull();
  });

  it("'lost' once, about 3 s after the last health event, then 'back' when they return", () => {
    const w = new HealthWatch(3000);
    for (let t = 0; t <= 5000; t += 1000) {
      expect(w.onHealth(t)).toBeNull();
      expect(w.check(t + 500)).toBeNull();
    }
    expect(w.check(7999)).toBeNull(); // last health at 5000
    expect(w.check(8000)).toBe("lost");
    expect(w.check(8250)).toBeNull(); // said once, not every check
    expect(w.isLost()).toBe(true);
    expect(w.onHealth(12_000)).toBe("back");
    expect(w.onHealth(13_000)).toBeNull();
    expect(w.isLost()).toBe(false);
  });

  it("reset forgets an outage (no 'Connected again' after re-pairing)", () => {
    const w = new HealthWatch(3000);
    w.onHealth(0);
    expect(w.check(3000)).toBe("lost");
    w.reset();
    expect(w.onHealth(4000)).toBeNull();
  });
});
