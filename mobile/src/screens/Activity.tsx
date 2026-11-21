// What the hat noticed recently (people and obstacles). Anonymous; purged after 30 minutes.
import { useEffect, useState } from "react";
import { Text } from "react-native";
import type { ActivityItem } from "@schema";
import { directionWords, obstacleWords, proximityWords, relativeTime } from "@web/a11y/describe";
import { errorText } from "../api";
import { useBrim, useLiveEvents } from "../BrimContext";
import { Body, ErrorText, Loading, Screen, styles, useColors } from "../ui";

let liveIds = -1; // negative ids for live items not yet in the DB listing

/** "Chair ahead, close by" — the same words the phone speaks, minus the full stop. */
function label(a: ActivityItem): string {
  const what = a.kind === "person" ? "Person" : obstacleWords(a.label ?? "obstacle");
  const parts = [directionWords(a.direction), proximityWords(a.proximity)].filter(Boolean);
  return parts.length ? `${what} ${parts.join(", ")}` : what;
}

export function ActivityScreen() {
  const c = useColors();
  const { api } = useBrim();
  const [items, setItems] = useState<ActivityItem[] | null>(null);
  const [error, setError] = useState<string | null>(null);
  const [, setTick] = useState(0);

  useEffect(() => {
    if (!api) return;
    api.activity(100).then(setItems, (e: unknown) => setError(errorText(e, "Couldn't load activity.")));
    const t = setInterval(() => setTick((n) => n + 1), 15000); // refresh "x minutes ago"
    return () => clearInterval(t);
  }, [api]);

  useLiveEvents((e) => {
    const seen_at = new Date().toISOString();
    let item: ActivityItem | null = null;
    if (e.type === "person_seen")
      item = { id: liveIds--, kind: "person", track_id: e.track_id, direction: e.direction, proximity: e.proximity, seen_at };
    if (e.type === "obstacle_seen")
      item = { id: liveIds--, kind: "obstacle", label: e.label, direction: e.direction, proximity: e.proximity, seen_at };
    if (item) setItems((xs) => [item, ...(xs ?? [])].slice(0, 200));
  });

  return (
    <Screen title="Activity" intro="What the hat noticed recently. Nobody is named or recognized, and everything here is deleted after 30 minutes.">
      <ErrorText error={error} />
      {items === null && !error ? <Loading what="activity" /> : null}
      {items?.length === 0 ? <Body>Nothing yet.</Body> : null}
      {items?.map((a) => (
        <Text key={a.id} style={[styles.item, { color: c.fg, borderColor: c.border }]}>
          <Text style={styles.bold}>{label(a)}</Text>
          <Text style={{ color: c.muted }}>, {relativeTime(a.seen_at)}</Text>
        </Text>
      ))}
    </Screen>
  );
}
