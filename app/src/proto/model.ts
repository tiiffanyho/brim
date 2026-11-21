// Phone prototype: settings, the D1–D5 demo walk, and how laptop-camera events become a screen.
import type { BrimEvent, ClearPathEvent, ObstacleSeenEvent, PersonSeenEvent, SafetyAlertEvent } from "@schema";

export type Tone = "calm" | "alert" | "safe";
export type Row = [key: string, value: string, highlight?: "red" | "green"];
export interface Screen {
  tone: Tone;
  quote: string;
  rows: Row[];
  next?: string;
  hint: string;
}

/* ---------------- Settings (the Figma Settings page) ---------------- */

export interface ProtoSettings {
  /** 0 = detect only, 1 = detect and warn, 2 = warn and guide. */
  level: 0 | 1 | 2;
  use: { following: boolean; phone: boolean; hazards: boolean; routes: boolean; people: boolean };
  sensitivity: "low" | "normal" | "high";
  warnAfterMin: 3 | 5 | 10;
  when: { speak: boolean; safePlace: boolean; share: boolean; describe: boolean };
  voice: "slow" | "normal" | "fast";
  contact: string;
  source: "demo" | "camera";
}

export const DEFAULTS: ProtoSettings = {
  level: 2,
  use: { following: true, phone: true, hazards: true, routes: false, people: false },
  sensitivity: "normal",
  warnAfterMin: 5,
  when: { speak: true, safePlace: true, share: true, describe: true },
  voice: "normal",
  contact: "Mom",
  source: "demo",
};

const KEY = "brim-proto-settings";
export function loadSettings(): ProtoSettings {
  try {
    const raw = localStorage.getItem(KEY);
    return raw ? { ...DEFAULTS, ...(JSON.parse(raw) as Partial<ProtoSettings>) } : DEFAULTS;
  } catch {
    return DEFAULTS;
  }
}
export function saveSettings(s: ProtoSettings) {
  try {
    localStorage.setItem(KEY, JSON.stringify(s));
  } catch {
    /* private mode: kept for this visit */
  }
}

export const LEVEL_TEXT = [
  "Notices concerns quietly and saves them for you to review.",
  "Speaks up the moment something needs your attention.",
  "Warns you, finds the next safest place and guides you there. Can alert a contact.",
] as const;

export const RATE = { slow: 0.9, normal: 1.1, fast: 1.3 } as const;

/* ---------------- Demo walk: the Figma D1–D5 story ---------------- */

export interface DemoStep {
  id: string;
  ms: number;
  speak: "nav" | "alert" | "status";
  screen: (s: ProtoSettings) => Screen;
}

const words = (n: number) => ({ 3: "three", 5: "five", 10: "ten" })[n] ?? String(n);

export const DEMO: DemoStep[] = [
  {
    id: "D1",
    ms: 7000,
    speak: "nav",
    screen: () => ({
      tone: "calm",
      quote: "Route is clear. About 8 minutes to home.",
      rows: [
        ["Watching", "Front of you · 2 people nearby"],
        ["Hazards", "None on your path"],
        ["Following", "No one"],
      ],
      hint: 'Say "What\'s around me?"',
    }),
  },
  {
    id: "D2",
    ms: 8000,
    speak: "alert",
    screen: (s) => ({
      tone: "alert",
      quote: `Heads up. The same person has been behind you for ${words(s.warnAfterMin)} minutes.`,
      rows: [
        ["Seen", `4 times in ${s.warnAfterMin} minutes`],
        ["Distance", "Steady at about 5 m"],
        ["Pattern", "Matches following", "red"],
        ["Confidence", s.sensitivity === "high" ? "Medium" : "High"],
      ],
      next: s.level === 2 && s.when.safePlace ? "Find the nearest safe place" : undefined,
      hint: `Say "Repeat" or "Call ${s.contact}"`,
    }),
  },
  {
    id: "D3",
    ms: 8000,
    speak: "nav",
    screen: (s) => ({
      tone: "alert",
      quote: "I found a safe place. A café 60 metres ahead, open and staffed.",
      rows: [
        ["Café · 60 m", "Open · staffed · well lit", "green"],
        ["Pharmacy · 220 m", "Open · farther away"],
        ["Bus shelter · 30 m", "Lit but empty · skipped"],
        ["Why", "Closest place with people inside"],
      ],
      next: s.when.share ? `Guide to the café and share location with ${s.contact}` : "Guide to the café",
      hint: `Say "Repeat" or "Call ${s.contact}"`,
    }),
  },
  {
    id: "D4",
    ms: 8000,
    speak: "nav",
    screen: (s) => ({
      tone: "alert",
      quote: "Keep walking straight. The café is 40 metres ahead on your right.",
      rows: [
        ["Person", "Still 6 m behind"],
        [s.contact, s.when.share ? "Notified · seeing your location" : "Not notified", s.when.share ? "green" : undefined],
        ["Path", "Clear, no obstacles"],
      ],
      next: "In 40 m: turn right through the door",
      hint: 'Say "Call 911" anytime',
    }),
  },
  {
    id: "D5",
    ms: 9000,
    speak: "status",
    screen: (s) => ({
      tone: "safe",
      quote: "You are in safety.",
      rows: [
        ["Incident", "Saved to your safety log"],
        ["Duration", "2 min from warning to safe", "green"],
        ...(s.when.describe ? ([["Description", "Saved: dark jacket, red backpack"]] as Row[]) : []),
      ],
      hint: `Say "Call ${s.contact}" or "I'm okay"`,
    }),
  },
];

/** Which demo steps run at this level: detect only never guides; warn stops after the warning. */
export function demoFor(s: ProtoSettings): DemoStep[] {
  const steps = s.use.following ? DEMO : DEMO.filter((d) => d.id === "D1");
  if (s.level === 2 && s.when.safePlace) return steps;
  return steps.filter((d) => d.id === "D1" || d.id === "D2");
}

/* ---------------- Laptop camera: events -> screen ---------------- */

export interface CameraView {
  lastSaid: { text: string; kind: "alert" | "nav"; at: number } | null;
  path: ClearPathEvent | null;
  obstacle: { e: ObstacleSeenEvent; at: number } | null;
  people: Map<number, { e: PersonSeenEvent; at: number }>;
  alertAt: number;
}

export const emptyCamera = (): CameraView => ({ lastSaid: null, path: null, obstacle: null, people: new Map(), alertAt: 0 });

export function onCameraEvent(v: CameraView, e: BrimEvent, now = Date.now()): CameraView {
  const next = { ...v, people: new Map(v.people) };
  if (e.type === "safety_alert") {
    next.lastSaid = { text: (e as SafetyAlertEvent).text, kind: "alert", at: now };
    next.alertAt = now;
  } else if (e.type === "clear_path") {
    next.lastSaid = { text: e.text, kind: "nav", at: now };
    next.path = e;
    if (e.clear && e.direction === "ahead" && !(e.blocked ?? []).length) next.alertAt = 0;
  } else if (e.type === "obstacle_seen") next.obstacle = { e, at: now };
  else if (e.type === "person_seen") next.people.set(e.track_id, { e, at: now });
  return next;
}

const side = (d: string) => (d === "ahead" ? "ahead" : `on your ${d}`);

export function cameraScreen(v: CameraView, now = Date.now()): Screen {
  const fresh = (at: number) => now - at < 5000;
  const alert = v.alertAt > 0 && now - v.alertAt < 6000;
  const obstacle = v.obstacle && fresh(v.obstacle.at) ? v.obstacle.e : null;
  const people = [...v.people.values()].filter((p) => fresh(p.at)).map((p) => p.e);
  const path = v.path;
  const blocked = path?.clear === false ? ["left", "ahead", "right"] : (path?.blocked ?? []);
  return {
    tone: alert ? "alert" : "calm",
    quote: v.lastSaid?.text ?? "Walk forward slowly. I'm watching the path.",
    rows: [
      ["Obstacle", obstacle ? `${obstacle.label} · ${side(obstacle.direction)} · ${obstacle.proximity}` : "None on your path", obstacle?.proximity === "close" ? "red" : undefined],
      ["People", people.length ? people.map((p) => `1 ${side(p.direction)}, ${p.proximity}`).join(" · ") : "No one close"],
      ["Path", blocked.length ? `Blocked ${blocked.join(", ")}` : "Clear ahead", blocked.length ? undefined : "green"],
    ],
    next: path
      ? !path.clear
        ? "Stop and wait. I'll say when a path clears."
        : path.direction === "ahead"
          ? "Keep going straight"
          : `Step ${path.direction}, then keep going straight`
      : undefined,
    hint: 'Say "What\'s around me?"',
  };
}
