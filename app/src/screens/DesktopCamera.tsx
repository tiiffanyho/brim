// Desktop camera dashboard (dev only, #/camera), built to the Figma "Desktop · Live camera" frame:
// purple sidebar, the hat's view with boxes, and the analysis on the side. All the work happens
// in vision/useBrimCamera.ts; this file is only layout.
import { useEffect, useRef, useState } from "react";
import { proximityOf } from "../vision/guide";
import { seenIsBrim } from "../vision/overlay";
import { useBrimCamera } from "../vision/useBrimCamera";
import { bunnySvg } from "../proto/bunnies";
import "./desktop.css";

const LANES = [
  ["left", "Left"],
  ["ahead", "Ahead"],
  ["right", "Right"],
] as const;

export function DesktopCameraScreen() {
  const cam = useBrimCamera();
  const h1 = useRef<HTMLHeadingElement>(null);
  const [calDist, setCalDist] = useState(2);
  const [calMsg, setCalMsg] = useState("");
  useEffect(() => {
    document.title = "Live camera – Brim";
    h1.current?.focus();
  }, []);

  const running = cam.status === "running";
  const inView = cam.tracks.filter((t) => seenIsBrim(t)).slice(0, 5);
  const clear = (d: string) => cam.frame.kind === "nocall" || !cam.frame.blocked.includes(d as never);

  return (
    <div className="dk">
      <aside className="dk-side" aria-label="Brim">
        <div className="dk-brand">
          <span className="dk-bunny" dangerouslySetInnerHTML={{ __html: bunnySvg("brim") }} />
          <span>Brim</span>
        </div>
        <nav aria-label="Pages">
          <ul>
            <li>
              <a href="#/camera" aria-current="page">Live camera</a>
            </li>
            <li>
              <a href="#/phone" target="_blank" rel="noreferrer">Phone view</a>
            </li>
            <li>
              <a href="#/live">Hat app</a>
            </li>
          </ul>
        </nav>
        <div className="dk-link">
          {cam.phones > 0 ? (
            <>
              <p className="dk-link-title">{cam.phones === 1 ? "1 phone linked" : `${cam.phones} phones linked`}</p>
              <p>The phone speaks; this laptop stays quiet.</p>
            </>
          ) : (
            <>
              <p className="dk-link-title">Link a phone</p>
              {cam.qr && <img src={cam.qr} alt="" width={132} height={132} />}
              <p>Same Wi-Fi. Scan, or open</p>
              {cam.phoneUrl && <code>{cam.phoneUrl}</code>}
            </>
          )}
        </div>
      </aside>

      <main className="dk-main" id="main">
        <header className="dk-top">
          <div>
            <p className="dk-kicker">Hat view</p>
            <h1 ref={h1} tabIndex={-1}>Live camera</h1>
          </div>
          <div className="dk-pills">
            <span className={`dk-pill ${running ? "live" : ""}`}>
              ● {running ? `LIVE · ${cam.stats.fps} fps · ${cam.stats.detectMs} ms` : cam.status === "loading" ? "LOADING" : "OFF"}
            </span>
            <span className="dk-pill soft">{cam.calibrated ? "Calibrated" : "Not calibrated"}</span>
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
        {cam.error && <p className="dk-error" role="alert">{cam.error}</p>}
        <div className="dk-under">
          <p>Nothing is recorded. Frames are drawn, then dropped.{cam.modelSource && ` Model: ${cam.modelSource}.`}</p>
          {running && (
            <button className="dk-ghost" onClick={cam.stop}>
              Stop camera
            </button>
          )}
        </div>
      </main>

      <section className="dk-panel" aria-label="Analysis">
        <div className={`dk-card says ${cam.said?.kind === "alert" ? "alert" : ""}`} aria-live="polite">
          <div className="dk-wave" aria-hidden="true">
            {[10, 20, 30, 14, 24, 34, 16, 26, 12].map((h, i) => (
              <i key={i} style={{ height: h }} />
            ))}
          </div>
          <p className="dk-kicker">Brim says{cam.said?.kind === "alert" ? " · safety first" : ""}</p>
          <p className="dk-quote">{cam.said?.text ?? (running ? "Watching the path…" : "Start the camera.")}</p>
          {cam.said && <p className="dk-small">Decided {cam.said.delayMs} ms after the frame</p>}
        </div>

        <div className="dk-card">
          <p className="dk-kicker">Path</p>
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
          <p className="dk-kicker">In view</p>
          {inView.length === 0 ? (
            <p className="dk-small">{running ? "Nothing Brim cares about." : "—"}</p>
          ) : (
            <ul className="dk-list">
              {inView.map((t) => {
                const p = proximityOf(t);
                const chip = t.approaching ? `Coming${t.timeToContactS ? ` ${t.timeToContactS.toFixed(1)} s` : ""}` : p[0]!.toUpperCase() + p.slice(1);
                const tone = t.approaching ? "red" : p === "close" ? "green" : "purple";
                return (
                  <li key={t.id}>
                    <span className={`dot ${tone}`} aria-hidden="true" />
                    <span className="grow">
                      <b>
                        #{t.id} {t.label}
                      </b>
                      <span>{t.distanceM !== null ? `about ${t.distanceM.toFixed(1)} m` : "distance unknown"}</span>
                    </span>
                    <span className={`chip ${tone}`}>{chip}</span>
                  </li>
                );
              })}
            </ul>
          )}
        </div>

        <div className="dk-card">
          <p className="dk-kicker">Timing</p>
          <div className="dk-metrics">
            <div>
              <b>{running ? `${cam.stats.detectMs} ms` : "—"}</b>
              <span>Detection</span>
            </div>
            <div>
              <b>{running ? `${cam.stats.fps} fps` : "—"}</b>
              <span>Frames</span>
            </div>
            <div>
              <b>{cam.said ? `${cam.said.delayMs} ms` : "—"}</b>
              <span>Frame to words</span>
            </div>
          </div>
        </div>

        <div className="dk-card">
          <p className="dk-kicker">Distance</p>
          <p className="dk-small">Stand a known distance away, whole body in view, then calibrate.</p>
          <div className="dk-cal">
            <label htmlFor="dk-cal">Metres</label>
            <input id="dk-cal" type="number" min={0.5} max={10} step={0.5} value={calDist} onChange={(e) => setCalDist(Number(e.target.value) || 2)} />
            <button className="dk-ghost" disabled={!running} onClick={() => setCalMsg(cam.calibrate(calDist))}>
              Calibrate
            </button>
          </div>
          {calMsg && <p className="dk-small" role="status">{calMsg}</p>}
          <div className="dk-check">
            <input id="dk-speak" type="checkbox" checked={cam.speakHere} onChange={(e) => cam.setSpeakHere(e.target.checked)} />
            <label htmlFor="dk-speak">Speak on this laptop when no phone is linked</label>
          </div>
        </div>

        <div className="dk-card">
          <p className="dk-kicker">Recent</p>
          {cam.recent.length === 0 ? (
            <p className="dk-small">Nothing said yet.</p>
          ) : (
            <ul className="dk-recent">
              {cam.recent.map((r) => (
                <li key={r.at + r.text} className={r.kind}>
                  <time>{new Date(r.at).toLocaleTimeString([], { hour: "2-digit", minute: "2-digit", second: "2-digit" })}</time>
                  <span>{r.text}</span>
                </li>
              ))}
            </ul>
          )}
        </div>
      </section>
    </div>
  );
}
