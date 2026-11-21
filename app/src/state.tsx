// App-wide state: pairing status, the live event stream, latest health, and screen-reader
// announcements for new events ("Sam is here", safety alerts).
import { createContext, useCallback, useContext, useEffect, useMemo, useRef, useState, type ReactNode } from "react";
import type { BrimEvent, FaultEvent, HealthEvent, HealthResponse } from "@schema";
import { api, ApiError } from "./api/client";
import { defaultWsUrl, EventStream, type StreamStatus } from "./api/ws";
import { useAnnouncer } from "./a11y/Announcer";
import { Deduper, describeEvent } from "./a11y/describe";

export type Connection = "checking" | "unpaired" | "online" | "offline";

interface BrimState {
  connection: Connection;
  stream: StreamStatus;
  health: HealthResponse | null;
  liveHealth: HealthEvent | null;
  faults: FaultEvent[];
  unreadAlerts: number;
  clearUnreadAlerts: () => void;
  subscribe: (fn: (e: BrimEvent) => void) => () => void;
  onPaired: () => void;
  refreshHealth: () => Promise<void>;
}

const Ctx = createContext<BrimState | null>(null);

export function useBrim(): BrimState {
  const v = useContext(Ctx);
  if (!v) throw new Error("useBrim outside BrimProvider");
  return v;
}

/** Calls fn for every live event while the component is mounted. */
export function useLiveEvents(fn: (e: BrimEvent) => void): void {
  const { subscribe } = useBrim();
  const ref = useRef(fn);
  useEffect(() => {
    ref.current = fn;
  });
  useEffect(() => subscribe((e) => ref.current(e)), [subscribe]);
}

export function BrimProvider({ children }: { children: ReactNode }) {
  const { announce } = useAnnouncer();
  const [connection, setConnection] = useState<Connection>("checking");
  const [stream, setStream] = useState<StreamStatus>("closed");
  const [health, setHealth] = useState<HealthResponse | null>(null);
  const [liveHealth, setLiveHealth] = useState<HealthEvent | null>(null);
  const [faults, setFaults] = useState<FaultEvent[]>([]);
  const [unreadAlerts, setUnreadAlerts] = useState(0);
  const subscribers = useRef(new Set<(e: BrimEvent) => void>());
  const deduper = useRef(new Deduper(30_000));
  const streamRef = useRef<EventStream | null>(null);

  const refreshHealth = useCallback(async () => {
    try {
      const h = await api.health();
      setHealth(h);
      setConnection("online");
    } catch (e) {
      setConnection(e instanceof ApiError && e.unauthorized ? "unpaired" : "offline");
    }
  }, []);

  const handleEvent = useCallback(
    (e: BrimEvent) => {
      if (e.type === "health") setLiveHealth(e);
      if (e.type === "fault") setFaults((f) => [e, ...f].slice(0, 20));
      if (e.type === "safety_alert") setUnreadAlerts((n) => n + 1);
      const a = describeEvent(e);
      const critical = e.type === "safety_alert" && e.severity === "critical";
      if (a && deduper.current.shouldAnnounce(a, Date.now(), critical)) announce(a.text, a.politeness);
      subscribers.current.forEach((fn) => fn(e));
    },
    [announce],
  );

  // Start the stream once; it reconnects by itself. A close re-checks pairing/offline state.
  useEffect(() => {
    const s = new EventStream({
      url: defaultWsUrl(),
      onEvent: handleEvent,
      onStatus: (st) => {
        setStream(st);
        if (st === "closed") void refreshHealth();
        if (st === "open") setConnection("online");
      },
    });
    streamRef.current = s;
    s.start();
    void refreshHealth();
    const onVisible = () => {
      if (document.visibilityState === "visible") s.reconnectNow();
    };
    document.addEventListener("visibilitychange", onVisible);
    return () => {
      document.removeEventListener("visibilitychange", onVisible);
      s.stop();
    };
  }, [handleEvent, refreshHealth]);

  // Poll server-side health every 5 s while paired (brim_connected, latency, restarts).
  useEffect(() => {
    if (connection === "unpaired") return;
    const t = setInterval(() => void refreshHealth(), 5000);
    return () => clearInterval(t);
  }, [connection, refreshHealth]);

  const subscribe = useCallback((fn: (e: BrimEvent) => void) => {
    subscribers.current.add(fn);
    return () => {
      subscribers.current.delete(fn);
    };
  }, []);

  const onPaired = useCallback(() => {
    setConnection("online");
    void refreshHealth();
    streamRef.current?.reconnectNow();
  }, [refreshHealth]);

  const clearUnreadAlerts = useCallback(() => setUnreadAlerts(0), []);

  const value = useMemo<BrimState>(
    () => ({ connection, stream, health, liveHealth, faults, unreadAlerts, clearUnreadAlerts, subscribe, onPaired, refreshHealth }),
    [connection, stream, health, liveHealth, faults, unreadAlerts, clearUnreadAlerts, subscribe, onPaired, refreshHealth],
  );
  return <Ctx.Provider value={value}>{children}</Ctx.Provider>;
}
