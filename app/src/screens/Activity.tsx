import { useEffect, useState } from "react";
import type { ActivityItem } from "@schema";
import { api } from "../api/client";
import { directionWords, obstacleWords, proximityWords, relativeTime } from "../a11y/describe";
import { useLiveEvents } from "../state";
import { ErrorMessage, Loading, Screen } from "./Screen";

let liveIds = -1; // negative ids for live items not yet in the DB listing

/** "Chair ahead, close by" — the same words the hat speaks, minus the full stop. */
function label(a: ActivityItem): string {
  const what = a.kind === "person" ? "Person" : obstacleWords(a.label ?? "obstacle");
  const parts = [directionWords(a.direction), proximityWords(a.proximity)].filter(Boolean);
  return parts.length ? `${what} ${parts.join(", ")}` : what;
}

export function ActivityScreen() {
  const [items, setItems] = useState<ActivityItem[] | null>(null);
  const [error, setError] = useState<string | null>(null);
  const [, setTick] = useState(0);

  useEffect(() => {
    api.activity(100).then(setItems, (e: unknown) => setError(e instanceof Error ? e.message : "Couldn't load activity."));
    const t = setInterval(() => setTick((n) => n + 1), 15000); // refresh "x minutes ago"
    return () => clearInterval(t);
  }, []);

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
    <Screen
      title="Activity"
      intro="What the hat noticed recently. Nobody is named or recognized, and everything here is deleted after 30 minutes."
    >
      <ErrorMessage error={error} />
      {items === null && !error && <Loading what="activity" />}
      {items?.length === 0 && <p>Nothing yet.</p>}
      {items && items.length > 0 && (
        <ol className="list" aria-label="Recent activity, newest first">
          {items.map((a) => (
            <li key={a.id}>
              <span className="strong">{label(a)}</span>
              <span className="muted"> · {relativeTime(a.seen_at)}</span>
            </li>
          ))}
        </ol>
      )}
    </Screen>
  );
}
