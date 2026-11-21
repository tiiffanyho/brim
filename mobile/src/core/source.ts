// Pure helpers for where events come from (hat or laptop camera) and what the phone says out loud.
// No React Native imports, so they are unit-tested (source.test.ts).
import type { Tier } from "./speech";

export const CAMERA_PORT = 5173;

/** How much Brim does: 0 detect only (shows, stays quiet), 1 warn (safety only), 2 warn and guide. */
export type Level = 0 | 1 | 2;

/** What the level lets the phone say out loud. Safety and connection status first. */
export function levelAllows(level: Level, tier: Tier): boolean {
  if (level === 0) return false;
  if (level === 1) return tier === "alert" || tier === "status";
  return true;
}

/** "192.168.1.20", "192.168.1.20:5173" or a pasted "http://…:5173/#/phone" -> "http://192.168.1.20:5173". */
export function normalizeCameraUrl(input: string): string | null {
  let s = input.trim().replace(/#.*$/, "");
  if (!s) return null;
  if (!/^https?:\/\//i.test(s)) s = `http://${s}`;
  const m = /^(https?):\/\/([A-Za-z0-9.-]+)(?::(\d{1,5}))?\/?$/.exec(s);
  if (!m) return null;
  const port = m[3] ? Number(m[3]) : CAMERA_PORT;
  if (port < 1 || port > 65535) return null;
  return `${m[1]!.toLowerCase()}://${m[2]!.toLowerCase()}:${port}`;
}

/** The laptop camera page relays the hat's events to phones here (app/vite.config.ts). */
export function cameraWsUrl(url: string): string {
  return `${url.replace(/^http/, "ws")}/camlink?role=phone`;
}
