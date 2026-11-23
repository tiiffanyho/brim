// The wearer's main screen (Figma D1–D5): the last thing Brim said, big; what Brim is looking at
// as plain rows; Brim the bunny; the voice bar to hear it again; settings top right. Text comes from
// the app's speech queue (BrimContext.recent), the rows from the live events (useLiveEvents).
import { useEffect, useState } from "react";
import { Pressable, StyleSheet, Text, View } from "react-native";
import type { ClearPathEvent, ObstacleSeenEvent, PersonSeenEvent } from "@schema";
import { useBrim, useLiveEvents } from "../BrimContext";
import { Bunny, GlowPage, Rows, VoiceBar, Wave } from "../design";
import { T, TONE_COLOR, tight, type Tone } from "../theme";
import { modeOf } from "../modes";
import { useFocusTitle } from "../ui";
import { say } from "../voice";

/** Live detections are forgotten after this long (nothing about people is kept). */
const TTL_MS = 5000;
/** A warning keeps the screen red this long, while the direction that follows is said. */
const ALERT_HOLD_MS = 6000;

const side = (d: string) => (d === "ahead" ? "ahead" : d === "unknown" ? "nearby" : `on your ${d}`);

export function LiveScreen({ openSettings }: { openSettings: () => void }) {
  const { recent, connection, linkLost, speaking, source, unreadAlerts, level, mode } = useBrim();
  const m = modeOf(mode);
  const title = useFocusTitle();
  const [path, setPath] = useState<{ e: ClearPathEvent; at: number } | null>(null);
  const [obstacle, setObstacle] = useState<{ e: ObstacleSeenEvent; at: number } | null>(null);
  const [people, setPeople] = useState<Map<number, { e: PersonSeenEvent; at: number }>>(new Map());
  const [now, setNow] = useState(Date.now());

  useEffect(() => {
    const t = setInterval(() => setNow(Date.now()), 500);
    return () => clearInterval(t);
  }, []);

  useLiveEvents((e) => {
    const at = Date.now();
    if (e.type === "clear_path") setPath({ e, at });
    else if (e.type === "obstacle_seen") setObstacle({ e, at });
    else if (e.type === "person_seen")
      setPeople((m) => {
        const n = new Map(m);
        n.set(e.track_id, { e, at });
        return n;
      });
  });

  const fresh = (at: number) => now - at < TTL_MS;
  const last = recent[0];
  const lastAlert = recent.find((r) => r.tier === "alert");
  const alertOn = !!lastAlert && now - lastAlert.at < ALERT_HOLD_MS;
  const ok = connection === "online" && !linkLost;
  // The mode's colour while calm; a warning is always red and "connected again" green.
  const tone: Tone = alertOn ? "alert" : last?.tier === "status" && /again|connected/i.test(last.text) && now - last.at < 4000 ? "safe" : "calm";

  const accent = tone === "calm" ? m.color : TONE_COLOR[tone];

  const status = !ok
    ? linkLost
      ? "Connection lost · trying again"
      : source === "camera"
        ? "Waiting for the laptop camera…"
        : "Can't reach your hat · trying again"
    : alertOn
      ? "Safety warning"
      : source === "camera"
        ? `Laptop camera · ${m.name}`
        : `Hat connected · ${m.name}`;

  const quote = last?.text ?? (ok ? m.idle : "Connecting…");
  const ob = obstacle && fresh(obstacle.at) ? obstacle.e : null;
  const ppl = [...people.values()].filter((p) => fresh(p.at)).map((p) => p.e);
  const p = path && fresh(path.at) ? path.e : null;
  const blocked = p ? (p.clear ? (p.blocked ?? []) : ["left", "ahead", "right"]) : [];
  const next = p
    ? !p.clear
      ? "Stop and wait. Brim will say when a path clears."
      : p.direction === "ahead"
        ? "Keep going straight"
        : `Step ${p.direction}, then keep going straight`
    : null;

  const rows: [string, string, ("red" | "green")?][] = [
    ["Obstacle", ob ? `${ob.label === "obstacle" ? "Something" : ob.label.charAt(0).toUpperCase() + ob.label.slice(1)} · ${side(ob.direction)} · ${ob.proximity}` : "None on your path", ob?.proximity === "close" ? "red" : undefined],
    ["People", ppl.length ? ppl.map((x) => `1 ${side(x.direction)}, ${x.proximity}`).join(" · ") : "No one close"],
    ["Path", blocked.length ? `Blocked ${blocked.join(", ")}` : "Clear ahead", blocked.length ? undefined : "green"],
  ];
  if (level === 0) rows.push(["Speech", "Detect only: Brim stays quiet"]);
  else if (!speaking) rows.push(["Speech", "Off. Turn on in Settings", "red"]);

  return (
    <GlowPage tone={tone} wash={tone === "calm" ? m.wash : undefined}>
      <View style={l.wrap}>
        <View style={l.top}>
          <Text accessibilityLiveRegion="polite" style={[l.status, { color: ok ? accent : T.red }]}>
            {status.toUpperCase()}
          </Text>
          <Pressable
            accessibilityRole="button"
            accessibilityLabel={unreadAlerts ? `Settings, ${unreadAlerts} new safety alerts` : "Settings"}
            onPress={openSettings}
            style={l.gear}
            hitSlop={6}
          >
            <View style={l.gearLines}>
              <View style={[l.gearLine, { width: 18 }]} />
              <View style={[l.gearKnob, { left: 9 }]} />
              <View style={[l.gearLine, { width: 18, marginTop: 7 }]} />
              <View style={[l.gearKnob, { left: 3, top: 9 }]} />
            </View>
            {unreadAlerts > 0 ? (
              <View style={l.badge}>
                <Text style={l.badgeText}>{unreadAlerts > 9 ? "9+" : unreadAlerts}</Text>
              </View>
            ) : null}
          </Pressable>
        </View>

        <View style={l.says}>
          <Wave tone={tone} color={accent} />
          <Text ref={title} accessibilityRole="header" style={l.hidden}>
            Brim says
          </Text>
          <Text accessibilityLiveRegion={alertOn ? "assertive" : "polite"} style={[l.quote, tight(30)]}>
            {quote}
          </Text>
        </View>

        <Bunny name={m.bunny} size={170} style={l.bunny} />

        <View style={{ gap: 14 }}>
          <Rows rows={rows} />
          {next ? (
            <View accessible accessibilityLabel={`Next step: ${next}`}>
              <Text style={l.nextLabel}>NEXT STEP</Text>
              <Text style={[l.next, tight(15)]}>{next}</Text>
            </View>
          ) : null}
        </View>

        <View style={l.foot}>
          <VoiceBar
            label={last ? "Tap to hear it again" : "Tap to hear Brim"}
            hint="Says the last thing Brim said."
            onPress={() => say(last?.text ?? quote, last?.tier === "alert" ? "assertive" : "polite")}
          />
        </View>
      </View>
    </GlowPage>
  );
}

const l = StyleSheet.create({
  wrap: { flex: 1, paddingTop: 58, paddingHorizontal: 20, paddingBottom: 30 },
  top: { flexDirection: "row", alignItems: "center", justifyContent: "space-between", minHeight: 48 },
  status: { fontSize: 12, fontWeight: "800", letterSpacing: 0.5, flex: 1, marginRight: 12 },
  gear: { width: 48, height: 48, borderRadius: 24, backgroundColor: "rgba(255,255,255,0.6)", alignItems: "center", justifyContent: "center" },
  gearLines: { width: 18, height: 18, justifyContent: "center" },
  gearLine: { height: 2, borderRadius: 1, backgroundColor: T.dark },
  gearKnob: { position: "absolute", top: 1, width: 6, height: 6, borderRadius: 3, borderWidth: 2, borderColor: T.dark, backgroundColor: "#fff" },
  badge: { position: "absolute", top: -2, right: -2, minWidth: 20, height: 20, borderRadius: 10, paddingHorizontal: 5, backgroundColor: T.red, alignItems: "center", justifyContent: "center" },
  badgeText: { color: "#fff", fontSize: 11, fontWeight: "800" },
  says: { marginTop: 40, gap: 14 },
  hidden: { position: "absolute", width: 1, height: 1, opacity: 0 },
  quote: { color: T.ink, fontWeight: "600", lineHeight: 38 },
  bunny: { alignSelf: "flex-end", marginVertical: 8 },
  nextLabel: { color: T.purple, fontSize: 11, fontWeight: "800", letterSpacing: 0.5 },
  next: { color: T.ink, fontWeight: "600", marginTop: 4, lineHeight: 21 },
  foot: { marginTop: "auto", paddingTop: 18 },
});
