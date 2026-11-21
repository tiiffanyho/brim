// Laptop camera test (dev only, #/camera). Runs the hat's detector model, tracker and the exact
// navigation rules from nav.cpp on your webcam so you can check what Brim would see and say.
// Nothing is recorded: frames are drawn and dropped; tracks are forgotten after 1.5 s.
import { useCallback, useEffect, useRef, useState } from "react";
import type { ObjectDetector } from "@mediapipe/tasks-vision";
import safetyJson from "../../../config/safety.json";
import { detect, loadDetector, type Seen } from "../vision/detector";
import { MIN_CONFIDENCE, NAV, Navigator, type Direction } from "../vision/nav";
import { focalFromVfov, Tracker, type Track } from "../vision/tracker";

type Status = "idle" | "loading" | "running" | "error";
const FOCAL_KEY = "brim-camera-focal";
const PURPLE = "#8b5cf6";
const RED = "#ef4444";
const GREEN = "#22c55e";
const GREY = "#9ca3af";

function savedFocal(): number {
  try {
    const v = Number(localStorage.getItem(FOCAL_KEY));
    return v > 0 ? v : focalFromVfov(45);
  } catch {
    return focalFromVfov(45);
  }
}

/** Same close/near/far rule as the hat (config/safety.json, box area fractions). */
function proximity(t: Track): "close" | "near" | "far" {
  const a = t.box.w * t.box.h;
  const close = t.thing === "person" ? safetyJson.person_close_area_frac : safetyJson.object_close_area_frac;
  return a >= close ? "close" : a >= safetyJson.near_area_frac ? "near" : "far";
}
function inCorridor(t: Track): boolean {
  const m = NAV.side_margin_frac;
  return t.box.y + t.box.h > 0.5 && t.box.x < 1 - m && t.box.x + t.box.w > m;
}

export function CameraTestScreen() {
  const h1 = useRef<HTMLHeadingElement>(null);
  const video = useRef<HTMLVideoElement>(null);
  const canvas = useRef<HTMLCanvasElement>(null);
  const detector = useRef<ObjectDetector | null>(null);
  const tracker = useRef(new Tracker(savedFocal()));
  const nav = useRef(new Navigator());
  const raf = useRef(0);
  const lastSpoken = useRef<{ text: string; at: number }>({ text: "", at: 0 });

  const [status, setStatus] = useState<Status>("idle");
  const [error, setError] = useState("");
  const [source, setSource] = useState("");
  const [speak, setSpeak] = useState(true);
  const [said, setSaid] = useState<{ text: string; kind: "alert" | "nav" } | null>(null);
  const [tracks, setTracks] = useState<Track[]>([]);
  const [stats, setStats] = useState({ fps: 0, ms: 0 });
  const [calDist, setCalDist] = useState(2);
  const [calMsg, setCalMsg] = useState("");

  useEffect(() => {
    document.title = "Camera test – Brim";
    h1.current?.focus();
  }, []);

  const say = useCallback(
    (text: string, kind: "alert" | "nav") => {
      const now = performance.now();
      // Same spirit as the hat: don't repeat the same words within a few seconds.
      if (lastSpoken.current.text === text && now - lastSpoken.current.at < 4000) return;
      lastSpoken.current = { text, at: now };
      setSaid({ text, kind });
      if (!speak || !("speechSynthesis" in window)) return;
      if (kind === "alert") window.speechSynthesis.cancel(); // safety interrupts
      window.speechSynthesis.speak(new SpeechSynthesisUtterance(text));
    },
    [speak],
  );

  const stop = useCallback(() => {
    cancelAnimationFrame(raf.current);
    const v = video.current;
    (v?.srcObject as MediaStream | null)?.getTracks().forEach((t) => t.stop());
    if (v) v.srcObject = null;
    setStatus((s) => (s === "error" ? s : "idle"));
  }, []);

  useEffect(() => () => {
    stop();
    detector.current?.close();
    if ("speechSynthesis" in window) window.speechSynthesis.cancel();
  }, [stop]);

  const loop = useCallback(() => {
    const v = video.current;
    const c = canvas.current;
    const d = detector.current;
    if (!v || !c || !d) return;
    let frames = 0;
    let windowStart = performance.now();
    let lastVideoTime = -1;
    const tick = () => {
      raf.current = requestAnimationFrame(tick);
      if (v.readyState < 2 || v.currentTime === lastVideoTime) return;
      lastVideoTime = v.currentTime;
      const now = performance.now();
      const seen: Seen[] = detect(d, v, now);
      const ms = performance.now() - now;
      const live = tracker.current.update(seen, now);

      // Exactly what the hat would act on: Brim things, confident, through nav.cpp's rules.
      const forNav = seen.filter((s) => s.brim && s.score >= MIN_CONFIDENCE);
      const call = nav.current.update(forNav, now);

      // Safety first: something close in the corridor, or something coming at you fast.
      const brimTracks = live.filter((t) => seenIsBrim(t) && t.score >= MIN_CONFIDENCE);
      const close = brimTracks.find((t) => t.thing !== "phone" && inCorridor(t) && proximity(t) === "close");
      const coming = live.find((t) => t.approaching && t.score >= MIN_CONFIDENCE);
      if (close) say(`${cap(close.thing === "unknown" ? "something" : close.label)} close ahead`, "alert");
      else if (coming) say(`${cap(coming.label)} approaching`, "alert");
      if (call) say(call.text, "nav");

      draw(c, v, live, nav.current.lastFrame.blocked, nav.current.lastFrame.kind);
      frames++;
      if (now - windowStart >= 500) {
        setStats({ fps: Math.round((frames * 1000) / (now - windowStart)), ms: Math.round(ms) });
        setTracks([...live].sort((a, b) => a.id - b.id));
        frames = 0;
        windowStart = now;
      }
    };
    tick();
  }, [say]);

  const start = async () => {
    setError("");
    setStatus("loading");
    try {
      if (!detector.current) {
        const { detector: det, modelSource } = await loadDetector(MIN_CONFIDENCE);
        detector.current = det;
        setSource(modelSource === "local" ? "models/efficientdet_lite0.tflite (same file as the hat)" : "Google's copy of EfficientDet-Lite0 (downloaded once)");
      }
      const stream = await navigator.mediaDevices.getUserMedia({ video: { width: 640, height: 480 }, audio: false });
      const v = video.current!;
      v.srcObject = stream;
      await v.play();
      tracker.current.reset();
      nav.current.reset();
      setStatus("running");
      loop();
    } catch (e) {
      setStatus("error");
      const msg = e instanceof Error ? e.message : String(e);
      setError(
        /Permission|NotAllowed/i.test(msg)
          ? "Camera blocked. Allow camera access for this page in your browser (and in System Settings → Privacy → Camera for your browser)."
          : /fetch|network|model/i.test(msg)
            ? "Couldn't load the detector model. Check you're online, or put efficientdet_lite0.tflite in the repo's models/ folder (see models/README.md)."
            : `Couldn't start: ${msg}`,
      );
    }
  };

  const calibrate = () => {
    const biggest = [...tracks].filter((t) => t.distanceM !== null).sort((a, b) => b.box.h - a.box.h)[0];
    if (!biggest || !biggest.distanceM) {
      setCalMsg("Nothing with a known size in view. Stand in frame, head to feet, and try again.");
      return;
    }
    // distance ∝ focal, so scale focal by (true / estimated).
    const f = tracker.current.focalFrac * (calDist / biggest.distanceM);
    tracker.current.focalFrac = f;
    try {
      localStorage.setItem(FOCAL_KEY, String(f));
    } catch {
      /* fine: kept for this visit */
    }
    setCalMsg(`Calibrated on the ${biggest.label} (#${biggest.id}) at ${calDist} m.`);
  };

  return (
    <section aria-labelledby="cam-title" className="screen camera-test">
      <p className="kicker">Laptop only · nothing is recorded</p>
      <h1 id="cam-title" ref={h1} tabIndex={-1}>
        Camera test
      </h1>
      <p className="intro">
        Your webcam stands in for the hat's camera. It runs the hat's detector, follows each thing with a number,
        guesses how far away it is, and says what Brim would say.
      </p>

      <div className="cam" data-status={status}>
        <video ref={video} playsInline muted aria-hidden="true" />
        <canvas ref={canvas} aria-hidden="true" />
        <div className="cam-hud" aria-hidden="true">
          <span className={status === "running" ? "rec on" : "rec"}>● {status === "running" ? "LIVE" : status === "loading" ? "LOADING" : "OFF"}</span>
          {status === "running" && (
            <span>
              {stats.fps} fps · {stats.ms} ms
            </span>
          )}
        </div>
        {status !== "running" && (
          <div className="cam-start">
            <button className="primary" onClick={start} disabled={status === "loading"}>
              {status === "loading" ? "Loading the model…" : "Start camera"}
            </button>
          </div>
        )}
      </div>
      {error && <p className="error" role="alert">{error}</p>}

      <div className={`card says-card ${said?.kind === "alert" ? "is-alert" : ""}`} aria-live="polite">
        <span className="kicker">Brim would say</span>
        <p className="quote">{said?.text ?? "Nothing yet. Walk toward the camera or put a chair in view."}</p>
      </div>

      <div className="row">
        {status === "running" && (
          <button onClick={stop}>Stop camera</button>
        )}
        <div className="check">
          <input id="cam-speak" type="checkbox" checked={speak} onChange={(e) => setSpeak(e.target.checked)} />
          <label htmlFor="cam-speak">Speak out loud</label>
        </div>
      </div>

      <h2>In view</h2>
      {tracks.length === 0 ? (
        <p className="muted">{status === "running" ? "Nothing detected." : "Start the camera to see detections."}</p>
      ) : (
        <ul className="list">
          {tracks.map((t) => (
            <li key={t.id}>
              <strong>
                #{t.id} {t.label}
              </strong>{" "}
              {Math.round(t.score * 100)}% · {proximity(t)}
              {t.distanceM !== null && <> · about {t.distanceM.toFixed(1)} m</>}
              {t.approaching && (
                <span className="error">
                  {" "}
                  · approaching{t.timeToContactS !== null && <>, reaches you in ~{t.timeToContactS.toFixed(1)} s</>}
                </span>
              )}
              {!seenIsBrim(t) && <span className="muted"> · the hat ignores this</span>}
            </li>
          ))}
        </ul>
      )}

      <h2>Distance calibration</h2>
      <p className="muted">
        There's no distance sensor, so distance comes from box size. For better numbers: stand a known distance from
        the camera with your whole body in view, enter it, then press Calibrate.
      </p>
      <div className="row">
        <label htmlFor="cal-dist" style={{ flex: 1 }}>
          I'm this many metres away
          <input id="cal-dist" type="number" min={0.5} max={10} step={0.5} value={calDist} onChange={(e) => setCalDist(Number(e.target.value) || 2)} />
        </label>
        <button onClick={calibrate} disabled={status !== "running"}>
          Calibrate
        </button>
      </div>
      {calMsg && <p className="ok" role="status">{calMsg}</p>}
      {source && <p className="muted">Model: {source}. Left and right are the camera's left and right, like the hat's.</p>}
    </section>
  );
}

const BRIM_LABELS = new Set(["person", "bicycle", "vehicle", "chair", "table", "phone", "bench", "backpack", "suitcase", "couch"]);
const seenIsBrim = (t: Track) => BRIM_LABELS.has(t.label);
const cap = (s: string) => s.charAt(0).toUpperCase() + s.slice(1);

/** Security-camera overlay: corridor, left/ahead/right bands, corner brackets and tags. */
export function draw(c: HTMLCanvasElement, v: HTMLVideoElement, tracks: Track[], blocked: Direction[], kind: string) {
  const W = v.videoWidth;
  const H = v.videoHeight;
  if (c.width !== W) c.width = W;
  if (c.height !== H) c.height = H;
  const g = c.getContext("2d");
  if (!g) return;
  g.clearRect(0, 0, W, H);
  const m = NAV.side_margin_frac;

  // Walking corridor (middle band, lower half) and band status.
  g.fillStyle = blocked.includes("ahead") ? "rgba(239,68,68,0.16)" : "rgba(139,92,246,0.12)";
  g.fillRect(W * m, H * 0.5, W * (1 - 2 * m), H * 0.5);
  g.setLineDash([8, 6]);
  g.strokeStyle = "rgba(255,255,255,0.55)";
  g.lineWidth = 1.5;
  g.beginPath();
  g.moveTo(W * m, 0);
  g.lineTo(W * m, H);
  g.moveTo(W * (1 - m), 0);
  g.lineTo(W * (1 - m), H);
  g.stroke();
  g.setLineDash([]);
  const bands: [Direction, string, number][] = [
    ["left", "LEFT", m / 2],
    ["ahead", "AHEAD", 0.5],
    ["right", "RIGHT", 1 - m / 2],
  ];
  g.font = "700 13px 'Figtree Variable', system-ui, sans-serif";
  g.textAlign = "center";
  for (const [dir, label, x] of bands) {
    const isBlocked = blocked.includes(dir);
    const text = `${label} · ${kind === "nocall" ? "—" : isBlocked ? "BLOCKED" : "CLEAR"}`;
    const tw = g.measureText(text).width + 16;
    g.fillStyle = kind === "nocall" ? "rgba(0,0,0,0.45)" : isBlocked ? "rgba(239,68,68,0.85)" : "rgba(34,197,94,0.85)";
    g.fillRect(W * x - tw / 2, H - 30, tw, 22);
    g.fillStyle = "#fff";
    g.fillText(text, W * x, H - 14);
  }

  // Boxes.
  g.textAlign = "left";
  for (const t of tracks) {
    const x = t.box.x * W;
    const y = t.box.y * H;
    const w = t.box.w * W;
    const h = t.box.h * H;
    const col = !seenIsBrim(t) ? GREY : t.approaching || (inCorridor(t) && proximity(t) === "close") ? RED : t.thing === "person" ? PURPLE : GREEN;
    const L = Math.max(10, Math.min(w, h) * 0.22);
    g.strokeStyle = col;
    g.lineWidth = 3;
    g.beginPath();
    for (const [px, py, dx, dy] of [
      [x, y, 1, 1],
      [x + w, y, -1, 1],
      [x, y + h, 1, -1],
      [x + w, y + h, -1, -1],
    ] as const) {
      g.moveTo(px + dx * L, py);
      g.lineTo(px, py);
      g.lineTo(px, py + dy * L);
    }
    g.stroke();
    g.lineWidth = 1;
    g.strokeStyle = col + "88";
    g.strokeRect(x, y, w, h);

    const parts = [`#${t.id} ${t.label.toUpperCase()}`, `${Math.round(t.score * 100)}%`];
    if (t.distanceM !== null) parts.push(`~${t.distanceM.toFixed(1)} m`);
    if (t.approaching) parts.push(t.timeToContactS ? `APPROACHING ${t.timeToContactS.toFixed(1)}s` : "APPROACHING");
    const tag = parts.join("  ");
    g.font = "700 12px ui-monospace, SFMono-Regular, Menlo, monospace";
    const tw = g.measureText(tag).width + 10;
    const ty = y > 20 ? y - 20 : y + 2;
    g.fillStyle = col;
    g.fillRect(x, ty, tw, 18);
    g.fillStyle = "#fff";
    g.fillText(tag, x + 5, ty + 13);
  }
}
