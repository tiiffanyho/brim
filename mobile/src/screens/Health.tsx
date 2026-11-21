// Device health: connection, the hat's own numbers, its components and recent faults.
import { Text } from "react-native";
import type { ComponentState } from "@schema";
import { useBrim } from "../BrimContext";
import { Body, Facts, H2, Screen, styles, useColors } from "../ui";

const STATE_LABEL: Record<ComponentState, string> = { ok: "Working", degraded: "Degraded", down: "Not working", stub: "Placeholder" };

function fmt(n: number | undefined, digits = 0, unit = ""): string {
  return n === undefined ? "unknown" : `${n.toFixed(digits)}${unit}`;
}

export function HealthScreen() {
  const c = useColors();
  const { health, liveHealth, faults, stream, connection } = useBrim();
  const h = liveHealth ?? health?.last_health;

  return (
    <Screen title="Device health">
      <H2>Connection</H2>
      <Facts
        rows={[
          ["Phone to hat", stream === "open" ? "Live" : connection === "offline" ? "Can't reach the hat" : "Reconnecting"],
          ["Camera system", health ? (health.brim_connected ? "Running" : "Not responding") : "unknown"],
          ["Restarts since boot", health ? String(health.restart_count) : "unknown"],
          ["Event delay inside the hat", fmt(health?.relay_latency_ms, 1, " ms")],
        ]}
      />

      <H2>Hat</H2>
      {h ? (
        <Facts
          rows={[
            ["Uptime", fmt(h.uptime_s / 60, 1, " minutes")],
            ["Camera frame rate", fmt(h.fps, 1, " frames per second")],
            ["Camera to event delay", fmt(h.pipeline_latency_ms, 0, " ms")],
            ["Processor use", fmt(h.cpu_pct, 0, "%")],
            ["Temperature", fmt(h.temp_c, 0, " °C")],
            ["Things waiting to be said", String(h.speak_queue_len)],
          ]}
        />
      ) : (
        <Body muted>No health report yet.</Body>
      )}

      {h ? (
        <>
          <H2>Components</H2>
          <Facts rows={h.components.map((x) => [x.name, `${STATE_LABEL[x.state]}${x.detail ? ` (${x.detail})` : ""}`] as [string, string])} />
        </>
      ) : null}

      <H2>Recent problems</H2>
      {faults.length === 0 ? (
        <Body muted>None since you opened the app.</Body>
      ) : (
        faults.map((f) => (
          <Text key={`${f.source}-${f.seq}-${f.t_mono_ms}`} style={[styles.item, { color: c.fg, borderColor: c.border }]}>
            <Text style={styles.bold}>{f.component}</Text> {f.action}: {f.message}
          </Text>
        ))
      )}
    </Screen>
  );
}
