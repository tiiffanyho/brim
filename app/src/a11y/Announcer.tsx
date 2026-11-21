// Two ARIA live regions (polite + assertive) that VoiceOver/TalkBack read without moving focus.
// Visually hidden but present in the accessibility tree from first render (regions added later
// are often ignored by screen readers).
import { createContext, useCallback, useContext, useMemo, useRef, useState, type ReactNode } from "react";
import type { Politeness } from "./describe";

interface AnnouncerApi {
  announce: (text: string, politeness?: Politeness) => void;
}

const Ctx = createContext<AnnouncerApi>({ announce: () => {} });

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

  const value = useMemo(() => ({ announce }), [announce]);
  return (
    <Ctx.Provider value={value}>
      {children}
      <div className="visually-hidden" role="status" aria-live="polite" aria-atomic="true">
        {polite}
      </div>
      <div className="visually-hidden" role="alert" aria-live="assertive" aria-atomic="true">
        {assertive}
      </div>
    </Ctx.Provider>
  );
}
