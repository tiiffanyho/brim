// The Brim look (Figma "Brim hat" file): soft purple, flat (no shadows), light glass on blurred
// purple-pink glows, Brim the bunny. Light only: the glows and bunnies are drawn for a light page.
export const T = {
  ink: "#1B1530",
  muted: "#5E5873",
  purple: "#7C3AED",
  button: "#6D28D9",
  dark: "#4C1D95",
  lavender: "#DDD3F7",
  tint: "#F3EEFF",
  bg: "#FAF8FF",
  red: "#DC2626",
  green: "#15803D",
  glass: "rgba(255,255,255,0.58)",
  glassStrong: "rgba(255,255,255,0.8)",
} as const;

export type Tone = "calm" | "alert" | "safe";

export const TONE_COLOR: Record<Tone, string> = { calm: T.purple, alert: T.red, safe: T.green };

/** Pre-rendered so the native build needs no drawing or blur library. */
export const GLOW = {
  calm: require("../assets/brim/glow-calm.jpg") as number,
  alert: require("../assets/brim/glow-alert.jpg") as number,
  safe: require("../assets/brim/glow-safe.jpg") as number,
};

export const BUNNY = {
  brim: require("../assets/brim/bunny-brim.png") as number,
  sit: require("../assets/brim/bunny-sit.png") as number,
  hop: require("../assets/brim/bunny-hop.png") as number,
  curious: require("../assets/brim/bunny-curious.png") as number,
  wave: require("../assets/brim/bunny-wave.png") as number,
  sleep: require("../assets/brim/bunny-sleep.png") as number,
};
export type BunnyName = keyof typeof BUNNY;

/** Slightly tight letter spacing, like Figtree in the designs. */
export const tight = (size: number) => ({ fontSize: size, letterSpacing: size >= 28 ? -0.9 : size >= 18 ? -0.35 : -0.15 });
