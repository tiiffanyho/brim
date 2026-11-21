// Security-camera overlay for the laptop camera: corridor, left/ahead/right bands, corner
// brackets and tags (id, label, confidence, distance, "approaching"). Pure drawing, no state.
import { inCorridor, proximityOf as proximity } from "./guide";
import { NAV, type Direction } from "./nav";
import type { Track } from "./tracker";

const PURPLE = "#a78bfa";
const RED = "#ef4444";
const GREEN = "#22c55e";
const GREY = "#9ca3af";

export const BRIM_LABELS = new Set(["person", "bicycle", "vehicle", "chair", "table", "phone", "bench", "backpack", "suitcase", "couch"]);
export const seenIsBrim = (t: Track) => BRIM_LABELS.has(t.label);

/** Security-camera overlay: corridor, left/ahead/right bands, corner brackets and tags. */
export function draw(c: HTMLCanvasElement, v: HTMLVideoElement, tracks: Track[], blocked: Direction[], kind: string) {
  const W = v.videoWidth;
  const H = v.videoHeight;
  if (c.width !== W) c.width = W;
  if (c.height !== H) c.height = H;
  const g = c.getContext("2d");
  if (!g) return;
  g.clearRect(0, 0, W, H);
  const m = NAV.side_margin_frac;

  // Walking corridor (middle band, lower half) and band status.
  g.fillStyle = blocked.includes("ahead") ? "rgba(239,68,68,0.16)" : "rgba(139,92,246,0.12)";
  g.fillRect(W * m, H * 0.5, W * (1 - 2 * m), H * 0.5);
  g.setLineDash([8, 6]);
  g.strokeStyle = "rgba(255,255,255,0.55)";
  g.lineWidth = 1.5;
  g.beginPath();
  g.moveTo(W * m, 0);
  g.lineTo(W * m, H);
  g.moveTo(W * (1 - m), 0);
  g.lineTo(W * (1 - m), H);
  g.stroke();
  g.setLineDash([]);
  const bands: [Direction, string, number][] = [
    ["left", "LEFT", m / 2],
    ["ahead", "AHEAD", 0.5],
    ["right", "RIGHT", 1 - m / 2],
  ];
  g.font = "700 13px 'Figtree Variable', system-ui, sans-serif";
  g.textAlign = "center";
  for (const [dir, label, x] of bands) {
    const isBlocked = blocked.includes(dir);
    const text = `${label} · ${kind === "nocall" ? "—" : isBlocked ? "BLOCKED" : "CLEAR"}`;
    const tw = g.measureText(text).width + 16;
    g.fillStyle = kind === "nocall" ? "rgba(0,0,0,0.45)" : isBlocked ? "rgba(239,68,68,0.85)" : "rgba(34,197,94,0.85)";
    g.fillRect(W * x - tw / 2, H - 30, tw, 22);
    g.fillStyle = "#fff";
    g.fillText(text, W * x, H - 14);
  }

  // Boxes.
  g.textAlign = "left";
  for (const t of tracks) {
    const x = t.box.x * W;
    const y = t.box.y * H;
    const w = t.box.w * W;
    const h = t.box.h * H;
    const col = !seenIsBrim(t) ? GREY : t.approaching || (inCorridor(t) && proximity(t) === "close") ? RED : t.thing === "person" ? PURPLE : GREEN;
    const L = Math.max(10, Math.min(w, h) * 0.22);
    g.strokeStyle = col;
    g.lineWidth = 3;
    g.beginPath();
    for (const [px, py, dx, dy] of [
      [x, y, 1, 1],
      [x + w, y, -1, 1],
      [x, y + h, 1, -1],
      [x + w, y + h, -1, -1],
    ] as const) {
      g.moveTo(px + dx * L, py);
      g.lineTo(px, py);
      g.lineTo(px, py + dy * L);
    }
    g.stroke();
    g.lineWidth = 1;
    g.strokeStyle = col + "88";
    g.strokeRect(x, y, w, h);

    const parts = [`#${t.id} ${t.label.toUpperCase()}`, `${Math.round(t.score * 100)}%`];
    if (t.distanceM !== null) parts.push(`~${t.distanceM.toFixed(1)} m`);
    if (t.approaching) parts.push(t.timeToContactS ? `APPROACHING ${t.timeToContactS.toFixed(1)}s` : "APPROACHING");
    const tag = parts.join("  ");
    g.font = "700 12px ui-monospace, SFMono-Regular, Menlo, monospace";
    const tw = g.measureText(tag).width + 10;
    const ty = y > 20 ? y - 20 : y + 2;
    g.fillStyle = col;
    g.fillRect(x, ty, tw, 18);
    g.fillStyle = "#fff";
    g.fillText(tag, x + 5, ty + 13);
  }
}
