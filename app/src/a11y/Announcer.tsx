// How the phone speaks: two ARIA live regions that VoiceOver/TalkBack read without moving focus.
//   aria-live="assertive"  safety_alert only — the screen reader interrupts what it is saying.
//   aria-live="polite"     everything else — read when the screen reader is idle.
// Visually hidden but present in the accessibility tree from first render (regions added later
// are often ignored by screen readers). The Pi plays no sound; this is Brim's only voice.
// speak() goes through SpeechScheduler (speech.ts) for the timing rules; announce() writes a
// region directly (for UI feedback such as "Settings saved").
import { createContext, useCallback, useContext, useEffect, useMemo, useRef, useState, type ReactNode } from "react";
import type { Politeness } from "./describe";
import { SpeechScheduler, type Tier } from "./speech";

interface AnnouncerApi {
  announce: (text: string, politeness?: Politeness) => void;
  speak: (text: string, tier: Tier) => void;
}

const Ctx = createContext<AnnouncerApi>({ announce: () => {}, speak: () => {} });

export function useAnnouncer(): AnnouncerApi {
  return useContext(Ctx);
}

export function AnnouncerProvider({ children }: { children: ReactNode }) {
  const [polite, setPolite] = useState("");
  const [assertive, setAssertive] = useState("");
  const timers = useRef<Record<Politeness, ReturnType<typeof setTimeout> | undefined>>({ polite: undefined, assertive: undefined });

  const announce = useCallback((text: string, politeness: Politeness = "polite") => {
    const set = politeness === "assertive" ? setAssertive : setPolite;
    // Clear first, then set on the next tick so identical consecutive messages are re-read.
    set("");
    clearTimeout(timers.current[politeness]);
    timers.current[politeness] = setTimeout(() => set(text), 60);
  }, []);

  const scheduler = useRef<SpeechScheduler | null>(null);
  if (!scheduler.current) scheduler.current = new SpeechScheduler(announce);
  useEffect(() => {
    const t = setInterval(() => scheduler.current?.pump(), 200);
    return () => clearInterval(t);
  }, []);
  const speak = useCallback((text: string, tier: Tier) => scheduler.current?.push(text, tier), []);

  const value = useMemo(() => ({ announce, speak }), [announce, speak]);
  return (
    <Ctx.Provider value={value}>
      {children}
      <div className="visually-hidden" role="status" aria-live="polite" aria-atomic="true" data-testid="live-polite">
        {polite}
      </div>
      <div className="visually-hidden" role="alert" aria-live="assertive" aria-atomic="true" data-testid="live-assertive">
        {assertive}
      </div>
    </Ctx.Provider>
  );
}
