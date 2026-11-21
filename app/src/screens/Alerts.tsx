import { useEffect, useState } from "react";
import type { Alert } from "@schema";
import { api } from "../api/client";
import { relativeTime } from "../a11y/describe";
import { useBrim, useLiveEvents } from "../state";
import { ErrorMessage, Loading, Screen } from "./Screen";

let liveIds = -1;

const SEVERITY_LABEL: Record<Alert["severity"], string> = { info: "Info", warning: "Warning", critical: "Danger" };

export function AlertsScreen() {
  const [alerts, setAlerts] = useState<Alert[] | null>(null);
  const [error, setError] = useState<string | null>(null);
  const { clearUnreadAlerts } = useBrim();

  useEffect(() => {
    clearUnreadAlerts();
    api.alerts(100).then(setAlerts, (e: unknown) => setError(e instanceof Error ? e.message : "Couldn't load alerts."));
  }, [clearUnreadAlerts]);

  useLiveEvents((e) => {
    if (e.type !== "safety_alert") return;
    clearUnreadAlerts();
    const a: Alert = { id: liveIds--, kind: e.kind, severity: e.severity, text: e.text, created_at: new Date().toISOString() };
    setAlerts((xs) => [a, ...(xs ?? [])].slice(0, 200));
  });

  return (
    <Screen title="Alerts" intro="Safety warnings the hat gave, newest first. The hat says these out loud too.">
      <ErrorMessage error={error} />
      {alerts === null && !error && <Loading what="alerts" />}
      {alerts?.length === 0 && <p>No alerts.</p>}
      {alerts && alerts.length > 0 && (
        <ol className="list" aria-label="Safety alerts, newest first">
          {alerts.map((a) => (
            <li key={a.id} className={`sev-${a.severity}`}>
              {/* Severity is spelled out in text, never colour alone. */}
              <span className="badge">{SEVERITY_LABEL[a.severity]}</span> <span className="strong">{a.text}</span>
              <span className="muted"> · {relativeTime(a.created_at)}</span>
            </li>
          ))}
        </ol>
      )}
    </Screen>
  );
}
