import { useEffect, useState } from "react";
import type { ActivityItem } from "@schema";
import { api } from "../api/client";
import { directionWords, relativeTime } from "../a11y/describe";
import { useLiveEvents } from "../state";
import { ErrorMessage, Loading, Screen } from "./Screen";

let liveIds = -1; // negative ids for live items not yet in the DB listing

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
    if (e.type === "person_recognized")
      item = { id: liveIds--, kind: "recognized", person_id: e.person_id, display_name: e.display_name, direction: e.direction, seen_at };
    if (e.type === "unknown_person") item = { id: liveIds--, kind: "stranger", anon_id: e.anon_id, direction: e.direction, seen_at };
    if (item) setItems((xs) => [item, ...(xs ?? [])].slice(0, 200));
  });

  return (
    <Screen title="Activity" intro="Who the hat noticed recently. Strangers are removed after 30 minutes.">
      <ErrorMessage error={error} />
      {items === null && !error && <Loading what="activity" />}
      {items?.length === 0 && <p>Nothing yet.</p>}
      {items && items.length > 0 && (
        <ol className="list" aria-label="Recent activity, newest first">
          {items.map((a) => {
            const who = a.kind === "recognized" ? (a.display_name ?? "Someone you know") : "Someone you don't know";
            const where = directionWords(a.direction);
            return (
              <li key={a.id}>
                <span className="strong">{who}</span>
                {where && `, ${where}`}
                <span className="muted"> · {relativeTime(a.seen_at)}</span>
              </li>
            );
          })}
        </ol>
      )}
    </Screen>
  );
}
