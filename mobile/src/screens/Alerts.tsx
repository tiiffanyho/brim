// Safety warnings the hat gave, newest first. Severity is spelled out, never colour alone.
import { useEffect, useState } from "react";
import { Text } from "react-native";
import type { Alert } from "@schema";
import { relativeTime } from "@web/a11y/describe";
import { errorText } from "../api";
import { useBrim, useLiveEvents } from "../BrimContext";
import { Body, ErrorText, Loading, Screen, styles, useColors } from "../ui";

let liveIds = -1;

const SEVERITY_LABEL: Record<Alert["severity"], string> = { info: "Info", warning: "Warning", critical: "Danger" };

export function AlertsScreen() {
  const c = useColors();
  const { api, clearUnreadAlerts } = useBrim();
  const [alerts, setAlerts] = useState<Alert[] | null>(null);
  const [error, setError] = useState<string | null>(null);

  useEffect(() => {
    clearUnreadAlerts();
    if (!api) return;
    api.alerts(100).then(setAlerts, (e: unknown) => setError(errorText(e, "Couldn't load alerts.")));
  }, [api, clearUnreadAlerts]);

  useLiveEvents((e) => {
    if (e.type !== "safety_alert") return;
    clearUnreadAlerts();
    const a: Alert = { id: liveIds--, kind: e.kind, severity: e.severity, text: e.text, created_at: new Date().toISOString() };
    setAlerts((xs) => [a, ...(xs ?? [])].slice(0, 200));
  });

  return (
    <Screen title="Alerts" intro="Safety warnings the hat gave, newest first. The phone says these out loud too.">
      <ErrorText error={error} />
      {alerts === null && !error ? <Loading what="alerts" /> : null}
      {alerts?.length === 0 ? <Body>No alerts.</Body> : null}
      {alerts?.map((a) => (
        <Text key={a.id} style={[styles.item, { color: c.fg, borderColor: c.border }]}>
          <Text style={[styles.bold, { color: a.severity === "info" ? c.fg : c.danger }]}>{SEVERITY_LABEL[a.severity]}: </Text>
          <Text style={styles.bold}>{a.text}</Text>
          <Text style={{ color: c.muted }}>, {relativeTime(a.created_at)}</Text>
        </Text>
      ))}
    </Screen>
  );
}
