// App-wide state for the native app. Same event handling as the web app's state.tsx: describeEvent /
// Deduper / SpeechScheduler / HealthWatch / EventStream (our own copies in src/core/) decide what is
// said and when; the difference is that here it is spoken by the phone itself (src/voice.ts) and
// keeps working in the background.
import AsyncStorage from "@react-native-async-storage/async-storage";
import { createContext, useCallback, useContext, useEffect, useMemo, useRef, useState, type ReactNode } from "react";
import { AccessibilityInfo, AppState, Vibration } from "react-native";
import type { BrimEvent, FaultEvent, HealthEvent, HealthResponse, Settings } from "@schema";
import { Deduper, describeEvent } from "./core/describe";
import { HealthWatch, SpeechScheduler, type Tier } from "./core/speech";
import { EventStream, type StreamStatus } from "./core/ws";
import { ApiError, makeApi, wsUrlFor, type HatApi } from "./api";
import { makeCameraApi } from "./camera";
import { cameraWsUrl, levelAllows, type Level } from "./core/source";
import { say, setVoice, startOnDuty, stopOnDuty } from "./voice";

/** No health event for this long = connection lost (health_period_ms is 1000 in config/app.json). */
export const HEALTH_TIMEOUT_MS = 3000;
const HAT_URL_KEY = "brim.hatUrl";
const SPEAK_KEY = "brim.speak";
const SOURCE_KEY = "brim.source";
const LEVEL_KEY = "brim.level";
const VIBRATE_KEY = "brim.vibrate";

const MAX_RECENT = 12;

/** Where events come from: a paired hat, or the laptop camera page (app/, `npm run camera`). */
export type Source = "hat" | "camera";
export type { Level };

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
  source: Source;
  /** Use the laptop camera instead of a hat (url from normalizeCameraUrl). */
  connectCamera: (url: string) => Promise<void>;
  level: Level;
  setLevel: (l: Level) => void;
  vibrate: boolean;
  setVibrate: (on: boolean) => void;
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
  const [source, setSource] = useState<Source>("hat");
  const [level, setLevelState] = useState<Level>(2);
  const [vibrate, setVibrateState] = useState(true);
  const levelRef = useRef<Level>(2);
  const vibrateRef = useRef(true);

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
        const tier = tierOf.current.get(text) ?? "info";
        const allowed = levelAllows(levelRef.current, tier);
        if (speakingRef.current && allowed) say(text, politeness);
        // Speech switched off: still let VoiceOver read it while the app is on screen.
        else if (allowed && AppState.currentState === "active") AccessibilityInfo.announceForAccessibility(text);
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

  const api: HatApi | null = useMemo(
    () => (hatUrl ? (source === "camera" ? makeCameraApi(hatUrl) : makeApi(hatUrl)) : null),
    [hatUrl, source],
  );

  // Load the saved hat address and the speech switch once.
  useEffect(() => {
    void (async () => {
      const [url, sp, src, lv, vib] = await Promise.all(
        [HAT_URL_KEY, SPEAK_KEY, SOURCE_KEY, LEVEL_KEY, VIBRATE_KEY].map((k) => AsyncStorage.getItem(k)),
      );
      if (sp === "off") {
        speakingRef.current = false;
        setSpeakingState(false);
      }
      if (src === "camera") setSource("camera");
      if (lv === "0" || lv === "1" || lv === "2") {
        levelRef.current = Number(lv) as Level;
        setLevelState(Number(lv) as Level);
      }
      if (vib === "off") {
        vibrateRef.current = false;
        setVibrateState(false);
      }
      setHatUrl(url ?? null);
      setConnection(url ? "offline" : "no-hat");
    })();
  }, []);

  const refreshHealth = useCallback(async () => {
    if (!api || source === "camera") return; // the laptop camera has no health endpoint
    try {
      setHealth(await api.health());
      setConnection("online");
    } catch (e) {
      setConnection(e instanceof ApiError && e.unauthorized ? "unpaired" : "offline");
    }
  }, [api, source]);

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
      if (e.type === "safety_alert") {
        setUnreadAlerts((n) => n + 1);
        if (vibrateRef.current && levelRef.current > 0) Vibration.vibrate(e.severity === "critical" ? [0, 220, 90, 220] : 160);
      }
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
    const camera = source === "camera";
    const s = new EventStream({
      url: camera ? cameraWsUrl(hatUrl) : wsUrlFor(hatUrl),
      onEvent: handleEvent,
      onStatus: (st) => {
        setStream(st);
        if (st === "closed") {
          if (camera) setConnection("offline");
          else void refreshHealth();
        }
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
  }, [hatUrl, source, handleEvent, refreshHealth, loadSettings]);

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
    if (connection === "unpaired" || connection === "no-hat" || connection === "loading" || source === "camera") return;
    const t = setInterval(() => void refreshHealth(), 5000);
    return () => clearInterval(t);
  }, [connection, source, refreshHealth]);

  // Stay alive in the background while paired and speaking; otherwise let iOS suspend us.
  useEffect(() => {
    const paired = connection === "online" || connection === "offline";
    if (paired && speaking) void startOnDuty().catch(() => undefined);
    else stopOnDuty();
  }, [connection, speaking]);

  const pair = useCallback(async (url: string, pin: string) => {
    await makeApi(url).pair(pin); // throws ApiError with a readable message
    await AsyncStorage.multiSet([
      [HAT_URL_KEY, url],
      [SOURCE_KEY, "hat"],
    ]);
    setSource("hat");
    healthWatch.current.reset();
    setHatUrl(url);
    setConnection("online");
    streamRef.current?.reconnectNow();
  }, []);

  const connectCamera = useCallback(async (url: string) => {
    await AsyncStorage.multiSet([
      [HAT_URL_KEY, url],
      [SOURCE_KEY, "camera"],
    ]);
    healthWatch.current.reset();
    setSource("camera");
    setHatUrl(url);
    setConnection("offline"); // "online" as soon as the camera link opens
  }, []);

  const setLevel = useCallback((l: Level) => {
    levelRef.current = l;
    setLevelState(l);
    void AsyncStorage.setItem(LEVEL_KEY, String(l));
  }, []);

  const setVibrate = useCallback((on: boolean) => {
    vibrateRef.current = on;
    setVibrateState(on);
    void AsyncStorage.setItem(VIBRATE_KEY, on ? "on" : "off");
  }, []);

  const forgetHat = useCallback(async () => {
    await AsyncStorage.multiRemove([HAT_URL_KEY, SOURCE_KEY]);
    setSource("hat");
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
      source,
      connectCamera,
      level,
      setLevel,
      vibrate,
      setVibrate,
    }),
    [hatUrl, api, connection, stream, linkLost, speaking, recent, health, liveHealth, faults, unreadAlerts, clearUnreadAlerts, subscribe, pair, forgetHat, setSpeaking, applySettings, source, connectCamera, level, setLevel, vibrate, setVibrate],
  );
  return <Ctx.Provider value={value}>{children}</Ctx.Provider>;
}
