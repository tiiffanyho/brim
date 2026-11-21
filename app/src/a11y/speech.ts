// When the phone says what. Speech comes from the phone's screen reader (VoiceOver / TalkBack)
// reading two ARIA live regions; this decides what goes into them and when. Pure logic with an
// injected clock, so it is unit-tested (speech.test.ts).
//
//   alert   safety_alert          -> assertive region at once: the screen reader interrupts itself.
//   nav     clear_path            -> polite region as soon as it arrives. Only waits while a safety
//                                    alert is still being read, so a direction never cuts off (or
//                                    comes before) the warning that made it necessary. Latest wins.
//                                    Not said at all when it is word for word the warning read in
//                                    the last repeatWindowMs ("Person ahead, move left" once).
//   status  faults, connection    -> polite, only when nothing else is being said. Never expires.
//   info    people, obstacles     -> polite, only when nothing else is being said; dropped if it
//                                    waited longer than infoMaxAgeMs (3 s): never said late.
//
// The screen reader does not tell a web page when it has finished talking, so "being said" is an
// estimate from the text length. Polite regions also wait for the screen reader to be idle on
// their own, so a too-short estimate only queues the next phrase, never cuts one off.
import type { Politeness } from "./describe";

export type Tier = "alert" | "nav" | "status" | "info";

export interface SpeechOptions {
  now?: () => number;
  infoMaxAgeMs?: number;
  /** Rough speaking time per character at the default screen-reader rate. */
  msPerChar?: number;
  minSpeakMs?: number;
  /** A direction identical to a warning said this recently is not said again. */
  repeatWindowMs?: number;
}

interface Pending {
  text: string;
  tier: Tier;
  at: number;
}

const ORDER: Tier[] = ["nav", "status", "info"];

export class SpeechScheduler {
  private readonly now: () => number;
  private readonly infoMaxAgeMs: number;
  private readonly msPerChar: number;
  private readonly minSpeakMs: number;
  private pending: Pending[] = [];
  private busyUntil = 0;
  private busyTier: Tier | null = null;
  private lastAlert: { text: string; at: number } | null = null;
  private readonly repeatWindowMs: number;

  constructor(
    private readonly out: (text: string, politeness: Politeness) => void,
    opts: SpeechOptions = {},
  ) {
    this.now = opts.now ?? (() => Date.now());
    this.infoMaxAgeMs = opts.infoMaxAgeMs ?? 3000;
    this.msPerChar = opts.msPerChar ?? 65;
    this.minSpeakMs = opts.minSpeakMs ?? 900;
    this.repeatWindowMs = opts.repeatWindowMs ?? 5000;
  }

  push(text: string, tier: Tier): void {
    const now = this.now();
    if (tier === "alert") {
      this.say(text, "assertive", tier, now);
      this.lastAlert = { text, at: now };
      return;
    }
    if (tier === "nav" && this.lastAlert && this.lastAlert.text === text && now - this.lastAlert.at <= this.repeatWindowMs) return;
    if (tier === "nav") this.pending = this.pending.filter((p) => p.tier !== "nav"); // latest direction wins
    if (tier === "status") this.pending = this.pending.filter((p) => !(p.tier === "status" && p.text === text));
    this.pending.push({ text, tier, at: now });
    this.pump();
  }

  /** Call on a timer (e.g. every 200 ms) so waiting phrases go out once the phone is quiet. */
  pump(): void {
    const now = this.now();
    this.pending = this.pending.filter((p) => p.tier !== "info" || now - p.at <= this.infoMaxAgeMs);
    const busy = now < this.busyUntil;
    for (const tier of ORDER) {
      if (busy && (tier !== "nav" || this.busyTier === "alert")) continue;
      const i = this.pending.findIndex((p) => p.tier === tier);
      if (i < 0) continue;
      const p = this.pending.splice(i, 1)[0]!;
      this.say(p.text, "polite", tier, now);
      return;
    }
  }

  /** True while the phone is (probably) still saying something. */
  speaking(): boolean {
    return this.now() < this.busyUntil;
  }

  waiting(): number {
    return this.pending.length;
  }

  private say(text: string, politeness: Politeness, tier: Tier, now: number): void {
    this.out(text, politeness);
    this.busyUntil = now + Math.max(this.minSpeakMs, text.length * this.msPerChar);
    this.busyTier = tier;
  }
}

/** "Connection lost" / "Connected again" from the 1 Hz health events (health_period_ms = 1000). */
export class HealthWatch {
  private lastHealth: number | null = null;
  private lost = false;

  constructor(private readonly timeoutMs = 3000) {}

  /** A health event arrived. Returns "back" when it ends an outage. */
  onHealth(now: number): "back" | null {
    this.lastHealth = now;
    if (!this.lost) return null;
    this.lost = false;
    return "back";
  }

  /** Call often (every ~250 ms). Returns "lost" once, when health has been missing for timeoutMs. */
  check(now: number): "lost" | null {
    if (this.lost || this.lastHealth === null) return null; // nothing to lose before the first one
    if (now - this.lastHealth < this.timeoutMs) return null;
    this.lost = true;
    return "lost";
  }

  isLost(): boolean {
    return this.lost;
  }

  /** Forget everything (e.g. after unpairing): no "Connected again" for a fresh start. */
  reset(): void {
    this.lastHealth = null;
    this.lost = false;
  }
}
