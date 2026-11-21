import type { ComponentState } from "@schema";
import { useBrim } from "../state";
import { Screen } from "./Screen";

const STATE_LABEL: Record<ComponentState, string> = { ok: "Working", degraded: "Degraded", down: "Not working", stub: "Placeholder" };

function fmt(n: number | undefined, digits = 0, unit = ""): string {
  return n === undefined ? "unknown" : `${n.toFixed(digits)}${unit}`;
}

export function DeviceHealthScreen() {
  const { health, liveHealth, faults, stream, connection } = useBrim();
  const h = liveHealth ?? health?.last_health;

  return (
    <Screen title="Device health">
      <h2>Connection</h2>
      <dl className="facts">
        <dt>Phone to hat</dt>
        <dd>{stream === "open" ? "Live" : connection === "offline" ? "Can't reach the hat" : "Reconnecting"}</dd>
        <dt>Camera system</dt>
        <dd>{health ? (health.brim_connected ? "Running" : "Not responding") : "unknown"}</dd>
        <dt>Restarts since boot</dt>
        <dd>{health?.restart_count ?? "unknown"}</dd>
        <dt>Event delay inside the hat</dt>
        <dd>{fmt(health?.relay_latency_ms, 1, " ms")}</dd>
      </dl>

      <h2>Hat</h2>
      {h ? (
        <dl className="facts">
          <dt>Uptime</dt>
          <dd>{fmt(h.uptime_s / 60, 1, " minutes")}</dd>
          <dt>Camera frame rate</dt>
          <dd>{fmt(h.fps, 1, " frames per second")}</dd>
          <dt>Camera to event delay</dt>
          <dd>{fmt(h.pipeline_latency_ms, 0, " ms")}</dd>
          <dt>Processor use</dt>
          <dd>{fmt(h.cpu_pct, 0, "%")}</dd>
          <dt>Temperature</dt>
          <dd>{fmt(h.temp_c, 0, " °C")}</dd>
          <dt>Things waiting to be said</dt>
          <dd>{h.speak_queue_len}</dd>
        </dl>
      ) : (
        <p className="muted">No health report yet.</p>
      )}

      {h && (
        <>
          <h2>Components</h2>
          <ul className="list">
            {h.components.map((c) => (
              <li key={c.name}>
                <span className="strong">{c.name}</span>: {STATE_LABEL[c.state]}
                {c.detail && <span className="muted"> ({c.detail})</span>}
              </li>
            ))}
          </ul>
        </>
      )}

      <h2>Recent problems</h2>
      {faults.length === 0 ? (
        <p className="muted">None since you opened the app.</p>
      ) : (
        <ol className="list">
          {faults.map((f) => (
            <li key={`${f.source}-${f.seq}`}>
              <span className="strong">{f.component}</span> {f.action}: {f.message}
            </li>
          ))}
        </ol>
      )}
    </Screen>
  );
}
