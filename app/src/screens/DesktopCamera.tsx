// Desktop camera dashboard (dev only, #/camera), built to the Figma frame "Desktop · Live camera":
// white page with soft glows, Brim top left, the hat's view in the middle, and three cards on the
// right (Brim says, Path, Recent). Calibration and phone linking live in the pills by the title.
// All the work happens in vision/useBrimCamera.ts; this file is only layout.
import { useEffect, useRef, useState } from "react";
import { useBrimCamera } from "../vision/useBrimCamera";
import { bunnySvg } from "../proto/bunnies";
import "./desktop.css";

const LANES = [
  ["left", "Left"],
  ["ahead", "Ahead"],
  ["right", "Right"],
] as const;

type Pop = null | "calibrate" | "phone";

export function DesktopCameraScreen() {
  const cam = useBrimCamera();
  const h1 = useRef<HTMLHeadingElement>(null);
  const [pop, setPop] = useState<Pop>(null);
  const [calDist, setCalDist] = useState(2);
  const [calAt, setCalAt] = useState<number | null>(null);
  const [calMsg, setCalMsg] = useState("");

  useEffect(() => {
    document.title = "Live camera – Brim";
    h1.current?.focus();
  }, []);
  useEffect(() => {
    if (!pop) return;
    const close = (e: KeyboardEvent) => e.key === "Escape" && setPop(null);
    window.addEventListener("keydown", close);
    return () => window.removeEventListener("keydown", close);
  }, [pop]);

  const running = cam.status === "running";
  const clear = (d: string) => cam.frame.kind === "nocall" || !cam.frame.blocked.includes(d as never);
  const alert = cam.said?.kind === "alert";

  return (
    <div className="dk">
      <div className="dk-glows" aria-hidden="true">
        <i className="g1" />
        <i className="g2" />
        <i className="g3" />
        <i className="g4" />
      </div>

      <div className="dk-brand">
        <span className="dk-bunny" aria-hidden="true" dangerouslySetInnerHTML={{ __html: bunnySvg("brim") }} />
        <span>Brim</span>
      </div>

      <main className="dk-main" id="main">
        <header className="dk-top">
          <h1 ref={h1} tabIndex={-1}>
            Live camera
          </h1>
          <div className="dk-pills">
            <span className={`dk-pill live ${running ? "" : "off"}`} role="status">
              ● {running ? `LIVE · ${cam.stats.fps} fps · ${cam.stats.detectMs} ms` : cam.status === "loading" ? "LOADING" : "OFF"}
            </span>
            <div className="dk-popwrap">
              <button className="dk-pill soft" aria-expanded={pop === "calibrate"} onClick={() => setPop(pop === "calibrate" ? null : "calibrate")}>
                {cam.calibrated ? (calAt ? `Calibrated at ${calAt} m` : "Calibrated") : "Calibrate"}
              </button>
              {pop === "calibrate" && (
                <div className="dk-pop" role="dialog" aria-label="Distance calibration">
                  <p>Stand a known distance away with your whole body in view, then calibrate.</p>
                  <div className="dk-cal">
                    <label htmlFor="dk-cal">Metres</label>
                    <input id="dk-cal" type="number" min={0.5} max={10} step={0.5} value={calDist} onChange={(e) => setCalDist(Number(e.target.value) || 2)} />
                    <button
                      className="dk-ghost"
                      disabled={!running}
                      onClick={() => {
                        const m = cam.calibrate(calDist);
                        setCalMsg(m);
                        if (m.startsWith("Calibrated")) setCalAt(calDist);
                      }}
                    >
                      Calibrate
                    </button>
                  </div>
                  {!running && <p className="dk-muted">Start the camera first.</p>}
                  {calMsg && <p role="status">{calMsg}</p>}
                </div>
              )}
            </div>
            <div className="dk-popwrap">
              <button className="dk-pill soft" aria-expanded={pop === "phone"} onClick={() => setPop(pop === "phone" ? null : "phone")}>
                {cam.phones > 0 ? `${cam.phones} phone${cam.phones > 1 ? "s" : ""} linked` : "Link a phone"}
              </button>
              {pop === "phone" && (
                <div className="dk-pop" role="dialog" aria-label="Link a phone">
                  {cam.qr && <img src={cam.qr} alt="" width={168} height={168} />}
                  <p>Same Wi-Fi. Scan with the phone camera, or in the Brim iPhone app choose “Laptop camera” and type:</p>
                  {cam.phoneUrl && <code>{cam.phoneUrl.replace(/^https?:\/\//, "").replace(/\/#\/phone$/, "")}</code>}
                  <div className="dk-check">
                    <input id="dk-speak" type="checkbox" checked={cam.speakHere} onChange={(e) => cam.setSpeakHere(e.target.checked)} />
                    <label htmlFor="dk-speak">Speak on this laptop when no phone is linked</label>
                  </div>
                </div>
              )}
            </div>
          </div>
        </header>

        <div className="dk-cam">
          <video ref={cam.video} playsInline muted aria-hidden="true" />
          <canvas ref={cam.canvas} aria-hidden="true" />
          {!running && (
            <div className="dk-start">
              <button className="dk-btn" onClick={cam.start} disabled={cam.status === "loading"}>
                {cam.status === "loading" ? "Loading the model…" : "Start camera"}
              </button>
              <p>Point the laptop the way you're walking. It plays the part of the hat.</p>
            </div>
          )}
        </div>
        {cam.error && (
          <p className="dk-error" role="alert">
            {cam.error}
          </p>
        )}
        <div className="dk-under">
          <p>Nothing is recorded. Frames are drawn, then dropped.</p>
          {running && (
            <button className="dk-link" onClick={cam.stop}>
              Stop camera
            </button>
          )}
        </div>
      </main>

      <section className="dk-panel" aria-label="Analysis">
        <div className={`dk-card says ${alert ? "alert" : ""}`} aria-live={alert ? "assertive" : "polite"}>
          <div className="dk-wave" aria-hidden="true">
            {[10, 20, 30, 14, 24, 34, 16, 26, 12].map((h, i) => (
              <i key={i} style={{ height: h }} />
            ))}
          </div>
          <h2 className="visually-hidden">Brim says</h2>
          <p className="dk-quote">{cam.said ? withStop(cam.said.text) : running ? "Watching the path…" : "Start the camera."}</p>
        </div>

        <div className="dk-card">
          <h2 className="dk-kicker">Path</h2>
          <ul className="dk-lanes">
            {LANES.map(([d, l]) => (
              <li key={d} className={clear(d) ? "clear" : "blocked"}>
                <b>{l}</b>
                {clear(d) ? "Clear" : "Blocked"}
              </li>
            ))}
          </ul>
        </div>

        <div className="dk-card">
          <h2 className="dk-kicker">Recent</h2>
          {cam.recent.length === 0 ? (
            <p className="dk-muted">Nothing said yet.</p>
          ) : (
            <ul className="dk-recent">
              {cam.recent.map((r) => (
                <li key={r.at + r.text} className={r.kind === "alert" ? "alert" : /^path clear/i.test(r.text) ? "ok" : ""}>
                  <time>{new Date(r.at).toLocaleTimeString([], { hour: "2-digit", minute: "2-digit", second: "2-digit", hour12: false })}</time>
                  <span>{withStop(r.text)}</span>
                </li>
              ))}
            </ul>
          )}
        </div>
      </section>
    </div>
  );
}

/** Phrases come without a final full stop (like the hat's); add it for reading. */
const withStop = (t: string) => (/[.!?]$/.test(t) ? t : `${t}.`);
