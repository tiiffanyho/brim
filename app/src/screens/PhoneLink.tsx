// Dev only (#/phone): the phone side of the laptop camera demo. Same Live screen as the real app,
// fed by the laptop's camera over the local Wi-Fi, and spoken out loud with the phone's own voice
// using the app's speech rules (safety first, a direction never before its warning, latest wins).
import { useCallback, useEffect, useRef, useState } from "react";
import type { BrimEvent } from "@schema";
import { openLink } from "../vision/link";
import { LiveView } from "./Live";

export function PhoneLinkScreen() {
  const [open, setOpen] = useState(false);
  const [voiceOn, setVoiceOn] = useState(false);
  const subscribers = useRef(new Set<(e: BrimEvent) => void>());
  const voice = useRef(false);
  const lastEventAt = useRef(0);
  const [stale, setStale] = useState(false);

  const voiceRef = useRef(new PhoneVoice(() => voice.current));

  useEffect(() => {
    document.title = "Brim (phone)";
    const link = openLink(
      "phone",
      (m) => {
        if (m.type === "peers") return;
        lastEventAt.current = Date.now();
        if (m.type === "safety_alert") {
          voiceRef.current.alert(m.text);
          navigator.vibrate?.(m.severity === "critical" ? [200, 80, 200] : 120);
        } else if (m.type === "clear_path") {
          voiceRef.current.nav(m.text);
        }
        for (const fn of subscribers.current) fn(m);
      },
      setOpen,
    );
    const pump = setInterval(() => {
      setStale(Date.now() - lastEventAt.current > 4000);
    }, 500);
    return () => {
      clearInterval(pump);
      link.close();
      window.speechSynthesis?.cancel();
    };
  }, []);

  const subscribe = useCallback((fn: (e: BrimEvent) => void) => {
    subscribers.current.add(fn);
    return () => subscribers.current.delete(fn);
  }, []);

  // Phones only allow speech (and keeping the screen on) after a tap.
  const start = async () => {
    voice.current = true;
    setVoiceOn(true);
    const u = new SpeechSynthesisUtterance("Brim is ready. Walk forward slowly.");
    u.rate = 1.1;
    window.speechSynthesis?.speak(u);
    try {
      await (navigator as Navigator & { wakeLock?: { request(t: "screen"): Promise<unknown> } }).wakeLock?.request("screen");
    } catch {
      /* not supported: keep the screen on by hand */
    }
  };

  return (
    <div className="app phone-link">
      <main id="main" tabIndex={-1}>
        <LiveView
          subscribe={subscribe}
          live={open}
          faults={[]}
          liveLabel={stale ? "Live · waiting for camera" : "Live · laptop camera"}
        />
      </main>
      {!voiceOn && (
        <div className="voice-gate">
          <button className="primary" onClick={start}>
            Tap to start Brim's voice
          </button>
          <p className="muted">{open ? "Connected to the laptop camera." : "Connecting to the laptop…"}</p>
        </div>
      )}
    </div>
  );
}

/**
 * The phone's own voice, timed by the voice itself (onend), not by a guess, so the next
 * instruction starts the moment the last one finishes. Same rules as a11y/speech.ts:
 * a safety alert cuts off anything; a direction is never cut off and never jumps ahead of a
 * warning, and only the latest waiting direction is said.
 */
class PhoneVoice {
  private speaking: "alert" | "nav" | null = null;
  private pendingNav: string | null = null;

  constructor(private enabled: () => boolean) {}

  alert(text: string) {
    this.pendingNav = null;
    this.say(text, "alert", true);
  }

  nav(text: string) {
    if (this.speaking) {
      // Never cut off (or jump ahead of) what is being said; only the newest direction waits.
      this.pendingNav = text;
      return;
    }
    this.say(text, "nav", false);
  }

  private say(text: string, kind: "alert" | "nav", interrupt: boolean) {
    if (!this.enabled() || !("speechSynthesis" in window)) return;
    const synth = window.speechSynthesis;
    if (interrupt) synth.cancel();
    const u = new SpeechSynthesisUtterance(text);
    u.rate = 1.1; // a little quicker: walking speed matters
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

  private current: SpeechSynthesisUtterance | null = null;
}
