// Everything the laptop camera does, as one hook: webcam -> detector (EfficientDet-Lite0, same
// model family as the hat) -> tracker + distance -> nav.cpp's rules (nav.ts) -> walking words
// (guide.ts) -> phones over the dev relay. Any page can use it: the desktop dashboard does.
// Nothing is recorded: frames are drawn, then dropped.
import { useCallback, useEffect, useRef, useState } from "react";
import type { ObjectDetector } from "@mediapipe/tasks-vision";
import type { BrimEvent } from "@schema";
import QRCode from "qrcode";
import { detect, loadDetector } from "./detector";
import { Guide } from "./guide";
import { openLink, type Link } from "./link";
import { MIN_CONFIDENCE, Navigator, type Direction, type FrameDecision } from "./nav";
import { draw } from "./overlay";
import { focalFromVfov, Tracker, type Track } from "./tracker";

export type CameraStatus = "idle" | "loading" | "running" | "error";
export interface Said {
  text: string;
  kind: "alert" | "nav";
  at: number;
  /** Camera frame to words, ms: the "camera to ear" number minus speaking time. */
  delayMs: number;
}

const FOCAL_KEY = "brim-camera-focal";
const FRAME_MS = 66; // the hat's 15 fps

function savedFocal(): number {
  try {
    const v = Number(localStorage.getItem(FOCAL_KEY));
    return v > 0 ? v : focalFromVfov(45);
  } catch {
    return focalFromVfov(45);
  }
}

export function useBrimCamera() {
  const video = useRef<HTMLVideoElement>(null);
  const canvas = useRef<HTMLCanvasElement>(null);
  const detector = useRef<ObjectDetector | null>(null);
  const tracker = useRef(new Tracker(savedFocal()));
  const nav = useRef(new Navigator());
  const guide = useRef(new Guide());
  const link = useRef<Link | null>(null);
  const phonesRef = useRef(0);
  const speakRef = useRef(true);
  const raf = useRef(0);
  const lastSpoken = useRef({ text: "", at: 0 });

  const [status, setStatus] = useState<CameraStatus>("idle");
  const [error, setError] = useState("");
  const [modelSource, setModelSource] = useState("");
  const [said, setSaid] = useState<Said | null>(null);
  const [recent, setRecent] = useState<Said[]>([]);
  const [tracks, setTracks] = useState<Track[]>([]);
  const [frame, setFrame] = useState<Pick<FrameDecision, "kind" | "blocked">>({ kind: "nocall", blocked: [] });
  const [stats, setStats] = useState({ fps: 0, detectMs: 0 });
  const [phones, setPhones] = useState(0);
  const [phoneUrl, setPhoneUrl] = useState("");
  const [qr, setQr] = useState("");
  const [speakHere, setSpeakHereState] = useState(true);
  const [calibrated, setCalibrated] = useState(() => {
    try {
      return !!localStorage.getItem(FOCAL_KEY);
    } catch {
      return false;
    }
  });

  // Phone link: events out, the wearer's settings in.
  useEffect(() => {
    link.current = openLink(
      "camera",
      (m) => {
        if (m.type === "peers") {
          phonesRef.current = m.phones;
          setPhones(m.phones);
        } else if (m.type === "proto_settings") {
          guide.current.setOptions({ sensitivity: m.sensitivity, hazards: m.hazards, people: m.people });
        }
      },
      () => {},
    );
    fetch("/camlink-info")
      .then((r) => r.json() as Promise<{ urls: string[] }>)
      .then(({ urls }) => {
        const u = urls[0];
        if (!u) return;
        setPhoneUrl(u);
        QRCode.toDataURL(u, { margin: 1, width: 240, color: { dark: "#2E1065", light: "#FFFFFF" } }).then(setQr, () => {});
      })
      .catch(() => {});
    return () => link.current?.close();
  }, []);

  const setSpeakHere = (v: boolean) => {
    speakRef.current = v;
    setSpeakHereState(v);
  };

  const say = useCallback((text: string, kind: "alert" | "nav", delayMs: number) => {
    const now = performance.now();
    if (lastSpoken.current.text === text && now - lastSpoken.current.at < 4000) return;
    lastSpoken.current = { text, at: now };
    const s: Said = { text, kind, at: Date.now(), delayMs: Math.round(delayMs) };
    setSaid(s);
    setRecent((r) => [s, ...r].slice(0, 8));
    // With a phone linked, the phone speaks; the laptop stays quiet.
    if (!speakRef.current || phonesRef.current > 0 || !("speechSynthesis" in window)) return;
    if (kind === "alert") window.speechSynthesis.cancel();
    const u = new SpeechSynthesisUtterance(text);
    u.rate = 1.1;
    window.speechSynthesis.speak(u);
  }, []);

  const stop = useCallback(() => {
    cancelAnimationFrame(raf.current);
    const v = video.current;
    (v?.srcObject as MediaStream | null)?.getTracks().forEach((t) => t.stop());
    if (v) v.srcObject = null;
    setStatus((s) => (s === "error" ? s : "idle"));
  }, []);

  useEffect(
    () => () => {
      stop();
      detector.current?.close();
      if ("speechSynthesis" in window) window.speechSynthesis.cancel();
    },
    [stop],
  );

  const loop = useCallback(() => {
    const v = video.current;
    const c = canvas.current;
    const d = detector.current;
    if (!v || !c || !d) return;
    let frames = 0;
    let windowStart = performance.now();
    let lastVideoTime = -1;
    let lastRun = 0;
    const tick = () => {
      raf.current = requestAnimationFrame(tick);
      if (v.readyState < 2 || v.currentTime === lastVideoTime) return;
      const now = performance.now();
      if (now - lastRun < FRAME_MS) return;
      lastRun = now;
      lastVideoTime = v.currentTime;
      const seen = detect(d, v, now);
      const detectMs = performance.now() - now;
      const live = tracker.current.update(seen, now);
      const call = nav.current.update(
        seen.filter((s) => s.brim && s.score >= MIN_CONFIDENCE),
        now,
      );
      const events: BrimEvent[] = guide.current.update({
        now,
        call,
        frame: nav.current.lastFrame,
        tracks: live,
        minConfidence: MIN_CONFIDENCE,
      });
      for (const e of events) {
        link.current?.send(e);
        if (e.type === "safety_alert") say(e.text, "alert", performance.now() - now);
        else if (e.type === "clear_path") say(e.text, "nav", performance.now() - now);
      }
      draw(c, v, live, nav.current.lastFrame.blocked, nav.current.lastFrame.kind);
      frames++;
      if (now - windowStart >= 400) {
        setStats({ fps: Math.round((frames * 1000) / (now - windowStart)), detectMs: Math.round(detectMs) });
        setTracks([...live].sort((a, b) => b.box.h - a.box.h));
        const lf = nav.current.lastFrame;
        setFrame({ kind: lf.kind, blocked: [...lf.blocked] as Direction[] });
        frames = 0;
        windowStart = now;
      }
    };
    tick();
  }, [say]);

  const start = useCallback(async () => {
    setError("");
    setStatus("loading");
    try {
      if (!detector.current) {
        const { detector: det, modelSource: src } = await loadDetector(MIN_CONFIDENCE);
        detector.current = det;
        setModelSource(src === "local" ? "models/efficientdet_lite0.tflite (the hat's file)" : "EfficientDet-Lite0 (downloaded once)");
      }
      const stream = await navigator.mediaDevices.getUserMedia({ video: { width: 640, height: 480 }, audio: false });
      const v = video.current!;
      v.srcObject = stream;
      await v.play();
      tracker.current.reset();
      nav.current.reset();
      guide.current.reset();
      setStatus("running");
      loop();
    } catch (e) {
      setStatus("error");
      const msg = e instanceof Error ? e.message : String(e);
      setError(
        /Permission|NotAllowed/i.test(msg)
          ? "Camera blocked. Allow camera access for this page in your browser (and in System Settings → Privacy → Camera)."
          : /fetch|network|model/i.test(msg)
            ? "Couldn't load the detector model. Check you're online, or put efficientdet_lite0.tflite in models/ (see models/README.md)."
            : `Couldn't start: ${msg}`,
      );
    }
  }, [loop]);

  /** Stand `metres` away, whole body in view: corrects the distance estimate for this webcam. */
  const calibrate = useCallback(
    (metres: number): string => {
      const biggest = tracks.filter((t) => t.distanceM !== null).sort((a, b) => b.box.h - a.box.h)[0];
      if (!biggest?.distanceM) return "Nothing with a known size in view. Stand in frame, head to feet, and try again.";
      const f = tracker.current.focalFrac * (metres / biggest.distanceM);
      tracker.current.focalFrac = f;
      try {
        localStorage.setItem(FOCAL_KEY, String(f));
      } catch {
        /* kept for this visit */
      }
      setCalibrated(true);
      return `Calibrated on the ${biggest.label} at ${metres} m.`;
    },
    [tracks],
  );

  return {
    video, canvas, status, error, modelSource, start, stop,
    said, recent, tracks, frame, stats,
    phones, phoneUrl, qr, speakHere, setSpeakHere, calibrate, calibrated,
  };
}
