// The soft background: blurred stars, triangles and a ribbon in purple-pink, never plain ovals
// (Figma D1–D5). Colours follow the mood; the shapes drift very slowly.
import { useMemo } from "react";
import type { Tone } from "./model";

const rng = (seed: number) => () => {
  seed |= 0;
  seed = (seed + 0x6d2b79f5) | 0;
  let t = Math.imul(seed ^ (seed >>> 15), 1 | seed);
  t = (t + Math.imul(t ^ (t >>> 7), 61 | t)) ^ t;
  return ((t ^ (t >>> 14)) >>> 0) / 4294967296;
};
function smooth(p: [number, number][]): string {
  const n = p.length;
  const f = (v: number) => v.toFixed(1);
  let d = `M${f(p[0]![0])} ${f(p[0]![1])}`;
  for (let i = 0; i < n; i++) {
    const p0 = p[(i - 1 + n) % n]!, p1 = p[i]!, p2 = p[(i + 1) % n]!, p3 = p[(i + 2) % n]!;
    d += `C${f(p1[0] + (p2[0] - p0[0]) / 6)} ${f(p1[1] + (p2[1] - p0[1]) / 6)} ${f(p2[0] - (p3[0] - p1[0]) / 6)} ${f(p2[1] - (p3[1] - p1[1]) / 6)} ${f(p2[0])} ${f(p2[1])}`;
  }
  return d + "Z";
}
function shape(cx: number, cy: number, r: number, inner: number, points: number, seed: number): string {
  const R = rng(seed);
  const k = inner ? points * 2 : points;
  const pts: [number, number][] = [];
  for (let i = 0; i < k; i++) {
    const a = -Math.PI / 2 + (i * 2 * Math.PI) / k;
    const rr = (inner && i % 2 ? r * inner : r) * (1 + (R() - 0.5) * 0.16);
    pts.push([cx + rr * Math.cos(a), cy + rr * Math.sin(a)]);
  }
  const out: [number, number][] = [];
  pts.forEach((a, i) => {
    const b = pts[(i + 1) % pts.length]!;
    out.push(a, [(a[0] + b[0]) / 2, (a[1] + b[1]) / 2]);
  });
  return smooth(out);
}

const PAL: Record<Tone, [string, string, string, string]> = {
  calm: ["#F472D0", "#A78BFA", "#93A5FB", "#F9A8D4"],
  alert: ["#FB7185", "#F472D0", "#C4B5FD", "#FDA4AF"],
  safe: ["#F0ABFC", "#A78BFA", "#86EFAC", "#F9A8D4"],
};

export function Glows({ tone = "calm", seed = 7 }: { tone?: Tone; seed?: number }) {
  const paths = useMemo(
    () => ({
      star: shape(170, 170, 170, 0.5, 5, seed + 1),
      tri: shape(150, 150, 150, 0, 3, seed + 2),
      small: shape(110, 110, 110, 0.48, 5, seed + 3),
      tri2: shape(120, 120, 120, 0, 3, seed + 4),
    }),
    [seed],
  );
  const [a, b, c, d] = PAL[tone];
  return (
    <svg className="pr-glows" viewBox="0 0 390 844" preserveAspectRatio="xMidYMid slice" aria-hidden="true" focusable="false">
      <defs>
        {(
          [
            ["g1", a, b],
            ["g2", c, b],
            ["g3", d, "#FBCFE8"],
            ["g4", b, c],
          ] as const
        ).map(([id, x, y]) => (
          <linearGradient key={id} id={`pr-${id}`} x1="0" x2="1">
            <stop offset="0" stopColor={x} />
            <stop offset="1" stopColor={y} />
          </linearGradient>
        ))}
      </defs>
      <g className="pr-drift a" transform="translate(-150 -20) rotate(-14 170 170) scale(1.3)">
        <path d={paths.star} fill="url(#pr-g1)" opacity="0.8" />
      </g>
      <g className="pr-drift b" transform="translate(160 220) rotate(22 150 150) scale(1.3)">
        <path d={paths.tri} fill="url(#pr-g2)" opacity="0.7" />
      </g>
      <g className="pr-drift c" transform="translate(-90 580) rotate(18 110 110) scale(1.3)">
        <path d={paths.small} fill="url(#pr-g3)" opacity="0.75" />
      </g>
      <g className="pr-drift a" transform="translate(220 560) rotate(-35 120 120) scale(1.3)">
        <path d={paths.tri2} fill="url(#pr-g4)" opacity="0.55" />
      </g>
    </svg>
  );
}
