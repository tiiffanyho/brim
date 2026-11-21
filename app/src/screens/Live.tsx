// Live "Brim says" screen (Figma D1–D5, A3, A4): the last thing the hat said, big, plus what it
// is looking at right now. Reads only from the shared event stream; nothing here is stored.
import { useEffect, useRef, useState } from "react";
import type { BrimEvent, ClearPathEvent, Direction, FaultEvent, ObstacleSeenEvent, PersonSeenEvent, SafetyAlertEvent } from "@schema";
import { describeEvent, directionWords, obstacleWords, proximityWords } from "../a11y/describe";
import { useBrim, useLiveEvents } from "../state";

type Tone = "calm" | "warn" | "alert" | "safe";
interface Said {
  text: string;
  tone: Tone;
  at: number;
  type: BrimEvent["type"];
}

/** Live detections are dropped after this long (privacy: nothing about people is kept). */
const LIVE_TTL_MS = 5000;
/** A safety alert stays on screen this long while Brim guides the wearer away from it. */
const ALERT_HOLD_MS = 6000;
const BANDS: { dir: Direction; label: string }[] = [
  { dir: "left", label: "Left" },
  { dir: "ahead", label: "Ahead" },
  { dir: "right", label: "Right" },
];

function toneOf(e: BrimEvent): Tone {
  switch (e.type) {
    case "safety_alert":
      return e.severity === "info" ? "warn" : "alert";
    case "clear_path":
      return e.clear ? "calm" : "alert";
    case "fault":
      return e.action === "recovered" ? "safe" : e.action === "degraded" ? "warn" : "calm";
    case "obstacle_seen":
      return e.proximity === "close" ? "warn" : "calm";
    default:
      return "calm";
  }
}

/** The words Brim spoke: the hat's own text where the schema carries it. */
function spokenText(e: BrimEvent): string | null {
  if (e.type === "safety_alert" || e.type === "clear_path") return e.text;
  if (e.type === "fault" && e.action === "degraded") return e.message;
  return describeEvent(e)?.text.replace(/\.$/, "") ?? null;
}

export function LiveScreen() {
  const { stream, faults } = useBrim();
  const h1 = useRef<HTMLHeadingElement>(null);
  const [said, setSaid] = useState<Said | null>(null);
  const [alert, setAlert] = useState<{ e: SafetyAlertEvent; at: number } | null>(null);
  const [path, setPath] = useState<{ e: ClearPathEvent; at: number } | null>(null);
  const [people, setPeople] = useState<Map<number, { e: PersonSeenEvent; at: number }>>(new Map());
  const [obstacle, setObstacle] = useState<{ e: ObstacleSeenEvent; at: number } | null>(null);
  const [now, setNow] = useState(() => Date.now());

  useEffect(() => {
    document.title = "Live – Brim";
    h1.current?.focus();
    const t = setInterval(() => setNow(Date.now()), 1000);
    return () => clearInterval(t);
  }, []);

  useLiveEvents((e) => {
    const at = Date.now();
    if (e.type === "health") return;
    if (e.type === "safety_alert") setAlert({ e, at });
    if (e.type === "clear_path") setPath({ e, at });
    if (e.type === "obstacle_seen") setObstacle({ e, at });
    if (e.type === "person_seen")
      setPeople((m) => {
        const next = new Map(m);
        next.set(e.track_id, { e, at });
        return next;
      });
    const text = spokenText(e);
    if (text) setSaid({ text, tone: toneOf(e), at, type: e.type });
  });

  const fresh = (at: number) => now - at < LIVE_TTL_MS;
  const activeFault: FaultEvent | undefined = faults[0]?.action === "degraded" ? faults[0] : undefined;
  const liveAlert = alert && now - alert.at < ALERT_HOLD_MS ? alert : null;
  const livePath = path && fresh(path.at) ? path.e : null;
  const liveObstacle = obstacle && fresh(obstacle.at) ? obstacle.e : null;
  const livePeople = [...people.values()].filter((p) => fresh(p.at)).map((p) => p.e);
  const guiding = liveAlert && said?.type === "clear_path" && said.at >= liveAlert.at;

  const tone: Tone = activeFault ? "warn" : liveAlert ? "alert" : (said?.tone ?? "calm");
  const quote = activeFault
    ? activeFault.message
    : guiding
      ? `${liveAlert.e.text}. ${said.text}.`
      : (said?.text ?? "Listening. Checking the path ahead.");
  const status = activeFault
    ? "Hat problem"
    : guiding
      ? "Safety warning · guiding"
      : liveAlert
        ? "Safety warning"
        : liveObstacle
          ? "Obstacle ahead"
          : tone === "safe"
            ? "Back to normal"
            : "Walking · all clear";

  const blocked = new Set<Direction>(livePath?.clear === false ? ["left", "ahead", "right"] : (livePath?.blocked ?? []));
  const next = activeFault
    ? "Check the brim isn't covered or tipped up. Brim will say when it's working again."
    : livePath
      ? !livePath.clear
        ? "Stop and wait. Brim will say when a path clears."
        : livePath.direction === "ahead"
          ? "Keep going straight"
          : `Step ${livePath.direction}, then keep going straight`
      : null;

  return (
    <section aria-labelledby="live-title" className={`screen live tone-${tone}`}>
      <div className="orb" aria-hidden="true" />
      <p className="live-status">
        <span className="dot" aria-hidden="true" />
        <span>{status}</span>
        <span className="live-conn">{stream === "open" ? "Live" : "Reconnecting…"}</span>
      </p>

      <div className="says">
        <div className="wave" aria-hidden="true">
          {[10, 20, 32, 16, 26, 36, 18, 28, 12, 22, 14].map((v, i) => (
            <i key={i} style={{ height: v, animationDelay: `${i * 0.08}s` }} />
          ))}
        </div>
        <h1 id="live-title" ref={h1} tabIndex={-1} className="kicker">
          Brim says
        </h1>
        {/* Not a live region: state.tsx already announces every event once. */}
        <p className="quote">{quote}</p>
        {said && <p className="muted">{ago(now - said.at)}</p>}
      </div>

      <section className="card analysis" aria-labelledby="analysis-title">
        <h2 id="analysis-title" className="kicker">
          {activeFault ? "What happened" : "What Brim is analyzing"}
        </h2>
        {activeFault ? (
          <dl className="facts">
            <dt>Part</dt>
            <dd className="hl">{activeFault.component}</dd>
            <dt>Warnings</dt>
            <dd>Paused until it recovers</dd>
          </dl>
        ) : (
          <>
            {livePath && (
              <ul className="bands" aria-label="Path ahead">
                {BANDS.map((b) => (
                  <li key={b.dir} className={blocked.has(b.dir) ? "blocked" : "clear"}>
                    <b>{b.label}</b>
                    {blocked.has(b.dir) ? "Blocked" : "Clear"}
                  </li>
                ))}
              </ul>
            )}
            <dl className="facts">
              <dt>Obstacle</dt>
              <dd className={liveObstacle ? "hl" : undefined}>
                {liveObstacle
                  ? [obstacleWords(liveObstacle.label), directionWords(liveObstacle.direction), proximityWords(liveObstacle.proximity)]
                      .filter(Boolean)
                      .join(" · ")
                  : "None on your path"}
              </dd>
              <dt>People</dt>
              <dd className={livePeople.some((p) => p.proximity === "close") ? "hl" : undefined}>
                {livePeople.length
                  ? livePeople.map((p) => [directionWords(p.direction), proximityWords(p.proximity)].filter(Boolean).join(", ") || "in view").join(" · ")
                  : "No one close"}
              </dd>
            </dl>
          </>
        )}
        {next && (
          <div className="next">
            <span className="kicker">{activeFault ? "To fix" : "Next step"}</span>
            <p>{next}</p>
          </div>
        )}
      </section>
      <p className="muted center">Everything here is also spoken in your earpiece.</p>
    </section>
  );
}

function ago(ms: number): string {
  const s = Math.round(ms / 1000);
  if (s < 5) return "just now";
  if (s < 60) return `${s} s ago`;
  return `${Math.round(s / 60)} min ago`;
}
