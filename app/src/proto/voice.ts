// The phone's voice for the prototype, timed by the voice itself (onend), so the next instruction
// starts the moment the last one finishes. Same rules as a11y/speech.ts: a safety alert cuts off
// anything; a direction is never cut off and never jumps ahead of a warning; only the newest
// waiting direction is said.
export class PhoneVoice {
  rate = 1.1;
  enabled = false;
  private speaking: "alert" | "nav" | null = null;
  private pendingNav: string | null = null;
  private current: SpeechSynthesisUtterance | null = null;

  alert(text: string) {
    this.pendingNav = null;
    this.say(text, "alert", true);
  }

  nav(text: string) {
    if (this.speaking) {
      this.pendingNav = text;
      return;
    }
    this.say(text, "nav", false);
  }

  /** Say something now, whatever is going on (a tap on the voice bar, a test phrase). */
  now(text: string) {
    this.pendingNav = null;
    this.say(text, "nav", true);
  }

  stop() {
    this.pendingNav = null;
    this.speaking = null;
    this.current = null;
    if ("speechSynthesis" in window) window.speechSynthesis.cancel();
  }

  private say(text: string, kind: "alert" | "nav", interrupt: boolean) {
    if (!this.enabled || !("speechSynthesis" in window)) return;
    const synth = window.speechSynthesis;
    if (interrupt) synth.cancel();
    const u = new SpeechSynthesisUtterance(text);
    u.rate = this.rate;
    u.lang = "en-US";
    u.onend = u.onerror = () => {
      if (this.current !== u) return;
      this.speaking = null;
      const next = this.pendingNav;
      this.pendingNav = null;
      if (next) this.say(next, "nav", false);
    };
    this.current = u;
    this.speaking = kind;
    synth.speak(u);
  }
}

/** Speech recognition where the browser has it (Chrome, Safari). Resolves with what was heard. */
export function listenOnce(): Promise<string | null> {
  type Rec = {
    lang: string;
    interimResults: boolean;
    maxAlternatives: number;
    onresult: ((e: { results: ArrayLike<ArrayLike<{ transcript: string }>> }) => void) | null;
    onerror: (() => void) | null;
    onend: (() => void) | null;
    start(): void;
  };
  const W = window as unknown as { SpeechRecognition?: new () => Rec; webkitSpeechRecognition?: new () => Rec };
  const Ctor = W.SpeechRecognition ?? W.webkitSpeechRecognition;
  if (!Ctor) return Promise.resolve(null);
  return new Promise((resolve) => {
    const r = new Ctor();
    r.lang = "en-US";
    r.interimResults = false;
    r.maxAlternatives = 1;
    let done = false;
    const finish = (v: string | null) => {
      if (!done) {
        done = true;
        resolve(v);
      }
    };
    r.onresult = (e) => finish(e.results[0]?.[0]?.transcript ?? null);
    r.onerror = () => finish(null);
    r.onend = () => finish(null);
    r.start();
  });
}

export const canListen = () =>
  typeof window !== "undefined" && ("SpeechRecognition" in window || "webkitSpeechRecognition" in window);
