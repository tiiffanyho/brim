// App-wide state for the native app. Same event handling as the web app's state.tsx: describeEvent /
// Deduper / SpeechScheduler / HealthWatch / EventStream (our own copies in src/core/) decide what is
// said and when; the difference is that here it is spoken by the phone itself (src/voice.ts) and
// keeps working in the background.
import AsyncStorage from "@react-native-async-storage/async-storage";
import { createContext, useCallback, useContext, useEffect, useMemo, useRef, useState, type ReactNode } from "react";
import { AccessibilityInfo, AppState } from "react-native";
import type { BrimEvent, FaultEvent, HealthEvent, HealthResponse, Settings } from "@schema";
import { Deduper, describeEvent } from "./core/describe";
import { HealthWatch, SpeechScheduler, type Tier } from "./core/speech";
import { EventStream, type StreamStatus } from "./core/ws";
import { ApiError, makeApi, wsUrlFor, type HatApi } from "./api";
import { say, setVoice, startOnDuty, stopOnDuty } from "./voice";

/** No health event for this long = connection lost (health_period_ms is 1000 in config/app.json). */
export const HEALTH_TIMEOUT_MS = 3000;
const HAT_URL_KEY = "brim.hatUrl";
const SPEAK_KEY = "brim.speak";
const MAX_RECENT = 12;

export type Connection = "loading" | "no-hat" | "unpaired" | "online" | "offline";

export interface Spoken {
  id: number;
  text: string;
  tier: Tier;
  at: number;
}

interface BrimState {
  hatUrl: string | null;
  api: HatApi | null;
  connection: Connection;
  stream: StreamStatus;
  linkLost: boolean;
  speaking: boolean;
  recent: Spoken[];
  health: HealthResponse | null;
  liveHealth: HealthEvent | null;
  faults: FaultEvent[];
  unreadAlerts: number;
  clearUnreadAlerts: () => void;
  /** Calls fn for every live event; returns an unsubscribe function. */
  subscribe: (fn: (e: BrimEvent) => void) => () => void;
  pair: (hatUrl: string, pin: string) => Promise<void>;
  forgetHat: () => Promise<void>;
  setSpeaking: (on: boolean) => void;
  /** The hat's settings were loaded or saved: apply the voice parts to the phone. */
  applySettings: (s: Settings) => void;
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
  const [hatUrl, setHatUrl] = useState<string | null>(null);
  const [connection, setConnection] = useState<Connection>("loading");
  const [stream, setStream] = useState<StreamStatus>("closed");
  const [linkLost, setLinkLost] = useState(false);
  const [speaking, setSpeakingState] = useState(true);
  const [recent, setRecent] = useState<Spoken[]>([]);
  const [health, setHealth] = useState<HealthResponse | null>(null);
  const [liveHealth, setLiveHealth] = useState<HealthEvent | null>(null);
  const [faults, setFaults] = useState<FaultEvent[]>([]);
  const [unreadAlerts, setUnreadAlerts] = useState(0);

  const speakingRef = useRef(true);
  const nextId = useRef(1);
  const deduper = useRef(new Deduper(30_000));
  const healthWatch = useRef(new HealthWatch(HEALTH_TIMEOUT_MS));
  const streamRef = useRef<EventStream | null>(null);
  const tierOf = useRef(new Map<string, Tier>());
  const subscribers = useRef(new Set<(e: BrimEvent) => void>());

  // What the scheduler lets through is spoken (if speech is on) and shown in "Recently said".
  const scheduler = useMemo(
    () =>
      new SpeechScheduler((text, politeness) => {
        if (speakingRef.current) say(text, politeness);
        // Speech switched off: still let VoiceOver read it while the app is on screen.
        else if (AppState.currentState === "active") AccessibilityInfo.announceForAccessibility(text);
        const tier = tierOf.current.get(text) ?? "info";
        setRecent((r) => [{ id: nextId.current++, text, tier, at: Date.now() }, ...r].slice(0, MAX_RECENT));
      }),
    [],
  );
  const speak = useCallback(
    (text: string, tier: Tier) => {
      tierOf.current.set(text, tier);
      scheduler.push(text, tier);
    },
    [scheduler],
  );

  const api: HatApi | null = useMemo(() => (hatUrl ? makeApi(hatUrl) : null), [hatUrl]);

  // Load the saved hat address and the speech switch once.
  useEffect(() => {
    void (async () => {
      const [url, sp] = await Promise.all([AsyncStorage.getItem(HAT_URL_KEY), AsyncStorage.getItem(SPEAK_KEY)]);
      if (sp === "off") {
        speakingRef.current = false;
        setSpeakingState(false);
      }
      setHatUrl(url);
      setConnection(url ? "offline" : "no-hat");
    })();
  }, []);

  const refreshHealth = useCallback(async () => {
    if (!api) return;
    try {
      setHealth(await api.health());
      setConnection("online");
    } catch (e) {
      setConnection(e instanceof ApiError && e.unauthorized ? "unpaired" : "offline");
    }
  }, [api]);

  const applySettings = useCallback((s: Settings) => setVoice(s.speech_rate, s.volume), []);

  const loadSettings = useCallback(async () => {
    if (!api) return;
    try {
      applySettings(await api.settings());
    } catch {
      /* keep the current voice */
    }
  }, [api, applySettings]);

  const handleEvent = useCallback(
    (e: BrimEvent) => {
      if (e.type === "health") {
        setLiveHealth(e);
        if (healthWatch.current.onHealth(Date.now()) === "back") {
          setLinkLost(false);
          speak("Connected again.", "status");
        }
      }
      if (e.type === "fault") setFaults((f) => [e, ...f].slice(0, 20));
      if (e.type === "safety_alert") setUnreadAlerts((n) => n + 1);
      const a = describeEvent(e);
      const always = a?.tier === "alert" || a?.tier === "nav";
      if (a && deduper.current.shouldAnnounce(a, Date.now(), always)) speak(a.text, a.tier);
      subscribers.current.forEach((fn) => fn(e));
    },
    [speak],
  );

  // One live stream per hat address; it reconnects by itself.
  useEffect(() => {
    if (!hatUrl) return;
    const s = new EventStream({
      url: wsUrlFor(hatUrl),
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
    void loadSettings();
    const sub = AppState.addEventListener("change", (st) => {
      if (st === "active") s.reconnectNow();
    });
    return () => {
      sub.remove();
      s.stop();
      streamRef.current = null;
    };
  }, [hatUrl, handleEvent, refreshHealth, loadSettings]);

  // Waiting phrases go out once the phone is quiet; runs in the background too (app kept alive).
  useEffect(() => {
    const t = setInterval(() => scheduler.pump(), 200);
    return () => clearInterval(t);
  }, [scheduler]);

  // "Connection lost" when health stops for HEALTH_TIMEOUT_MS, then a fresh socket.
  useEffect(() => {
    if (connection === "unpaired" || connection === "no-hat") {
      healthWatch.current.reset();
      setLinkLost(false);
      return;
    }
    const t = setInterval(() => {
      if (healthWatch.current.check(Date.now()) !== "lost") return;
      setLinkLost(true);
      speak("Connection lost.", "status");
      streamRef.current?.forceReconnect();
    }, 250);
    return () => clearInterval(t);
  }, [connection, speak]);

  // Server-side health (restarts, relay latency) every 5 s while paired.
  useEffect(() => {
    if (connection === "unpaired" || connection === "no-hat" || connection === "loading") return;
    const t = setInterval(() => void refreshHealth(), 5000);
    return () => clearInterval(t);
  }, [connection, refreshHealth]);

  // Stay alive in the background while paired and speaking; otherwise let iOS suspend us.
  useEffect(() => {
    const paired = connection === "online" || connection === "offline";
    if (paired && speaking) void startOnDuty().catch(() => undefined);
    else stopOnDuty();
  }, [connection, speaking]);

  const pair = useCallback(async (url: string, pin: string) => {
    await makeApi(url).pair(pin); // throws ApiError with a readable message
    await AsyncStorage.setItem(HAT_URL_KEY, url);
    healthWatch.current.reset();
    setHatUrl(url);
    setConnection("online");
    streamRef.current?.reconnectNow();
  }, []);

  const forgetHat = useCallback(async () => {
    await AsyncStorage.removeItem(HAT_URL_KEY);
    stopOnDuty();
    setHatUrl(null);
    setHealth(null);
    setLiveHealth(null);
    setRecent([]);
    setFaults([]);
    setUnreadAlerts(0);
    setConnection("no-hat");
  }, []);

  const setSpeaking = useCallback((on: boolean) => {
    speakingRef.current = on;
    setSpeakingState(on);
    void AsyncStorage.setItem(SPEAK_KEY, on ? "on" : "off");
  }, []);

  const subscribe = useCallback((fn: (e: BrimEvent) => void) => {
    subscribers.current.add(fn);
    return () => {
      subscribers.current.delete(fn);
    };
  }, []);

  const clearUnreadAlerts = useCallback(() => setUnreadAlerts(0), []);

  const value = useMemo<BrimState>(
    () => ({
      hatUrl,
      api,
      connection,
      stream,
      linkLost,
      speaking,
      recent,
      health,
      liveHealth,
      faults,
      unreadAlerts,
      clearUnreadAlerts,
      subscribe,
      pair,
      forgetHat,
      setSpeaking,
      applySettings,
    }),
    [hatUrl, api, connection, stream, linkLost, speaking, recent, health, liveHealth, faults, unreadAlerts, clearUnreadAlerts, subscribe, pair, forgetHat, setSpeaking, applySettings],
  );
  return <Ctx.Provider value={value}>{children}</Ctx.Provider>;
}
