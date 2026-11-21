import { describe, expect, it } from "vitest";
import { distanceWords, Guide, walkingWords } from "./guide";
import { Navigator, type Thing } from "./nav";
import { Tracker } from "./tracker";

describe("walking words", () => {
  it("rounds distance to half metres", () => {
    expect(distanceWords(0.5)).toBe("very close");
    expect(distanceWords(1.1)).toBe("1 metre");
    expect(distanceWords(1.4)).toBe("1.5 metres");
    expect(distanceWords(null)).toBe("");
  });
  it("words nav.cpp's calls for walking", () => {
    const go = (direction: "left" | "right" | "ahead", blocked: ("left" | "ahead" | "right")[]) => ({
      clear: true, direction, blocked, text: "",
    });
    expect(walkingWords(go("right", ["ahead", "left"]), "chair", 1.4)).toBe("Chair ahead, 1.5 metres. Step right");
    expect(walkingWords(go("left", ["right"]), "table", null)).toBe("Table on your right. Step left");
    expect(walkingWords(go("ahead", ["left", "right"]), "chair", null)).toBe("Gap ahead. Keep going straight");
    expect(walkingWords({ clear: false, direction: "unknown", blocked: [], text: "" }, "unknown" as Thing, null)).toBe(
      "Stop. Path blocked",
    );
  });
});

describe("guide", () => {
  const chair = (x: number, h = 0.45, w = 0.22) => ({ thing: "chair" as const, label: "chair", score: 0.9, box: { x, y: 0.4, w, h } });

  function run(frames: (typeof chair extends (...a: never[]) => infer R ? R : never)[][], start = 0) {
    const tr = new Tracker();
    const nav = new Navigator();
    const g = new Guide();
    const said: string[] = [];
    let t = start;
    for (const dets of frames) {
      t += 66;
      const tracks = tr.update(dets, t);
      const call = nav.update(dets, t);
      for (const e of g.update({ now: t, call, frame: nav.lastFrame, tracks, minConfidence: 0.6 }))
        if (e.type === "clear_path" || e.type === "safety_alert") said.push(e.text);
    }
    return said;
  }

  it("says a step, then 'Path clear' once the way opens", () => {
    const said = run([...Array(6).fill([chair(0.05)]), ...Array(30).fill([])]);
    expect(said[0]).toBe("Chair on your left. Step right");
    expect(said.at(-1)).toBe("Path clear. Walk forward");
  });

  it("warns first when something is close right in front", () => {
    const said = run(Array(6).fill([chair(0.25, 0.55, 0.5)])); // fills 27% of the frame: close
    expect(said[0]).toBe("Stop. Chair right in front of you");
    expect(said[1]).toMatch(/^Chair ahead, .+\. Step (left|right)$/);
  });
});
