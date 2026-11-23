// Modes: where the wearer is using Brim. A mode changes how the app presents Brim (name, bunny, the
// idle line on the Live screen); the hat's detection, safety warnings and directions are the same in
// every mode. Kept on this phone only.
import type { BunnyName } from "./theme";

export type ModeId = "everyday" | "hiking" | "travel" | "home" | "night";

export interface Mode {
  id: ModeId;
  name: string;
  /** One line under the name in Settings. Only mentions things the hat can detect. */
  blurb: string;
  /** What the Live screen says before Brim has said anything. */
  idle: string;
  bunny: BunnyName;
  /** Accent for text, the wave and the selected row. Dark enough for text on the light page (4.5:1). */
  color: string;
  /** See-through tint laid over the Live screen's glow. */
  wash: string;
}

export const MODES: readonly Mode[] = [
  {
    id: "everyday",
    name: "Everyday walk",
    blurb: "People and obstacles ahead, and which way is clear.",
    idle: "Walk forward slowly. I'm watching the path.",
    bunny: "brim",
    color: "#7C3AED",
    wash: "rgba(124,58,237,0)",
  },
  {
    id: "hiking",
    name: "Hiking",
    blurb: "Other hikers, bikes and anything in the way on the trail.",
    idle: "Trail mode. I'm watching the trail ahead.",
    bunny: "hop",
    color: "#1F7A3A",
    wash: "rgba(34,160,80,0.22)",
  },
  {
    id: "travel",
    name: "Travel",
    blurb: "Busy streets and stations: people close by, vehicles, bikes, and phones pointed at you.",
    idle: "Travel mode. I'm watching the crowd around you.",
    bunny: "curious",
    color: "#1D4ED8",
    wash: "rgba(37,99,235,0.22)",
  },
  {
    id: "home",
    name: "Home",
    blurb: "Chairs, tables, doors and people around the house.",
    idle: "Home mode. I'm watching for chairs, tables and people.",
    bunny: "sit",
    color: "#8B5A2B",
    wash: "rgba(150,95,45,0.24)",
  },
  {
    id: "night",
    name: "Night walk",
    blurb: "Extra eyes after dark. Works best under streetlights.",
    idle: "Night mode. I'm watching the path in the dark.",
    bunny: "sleep",
    color: "#1E3A8A",
    wash: "rgba(30,58,138,0.34)",
  },
];

export const DEFAULT_MODE: ModeId = "everyday";

export const modeOf = (id: ModeId): Mode => MODES.find((m) => m.id === id) ?? (MODES[0] as Mode);

export const isModeId = (v: string | null | undefined): v is ModeId => MODES.some((m) => m.id === v);
