// Settings (Figma, flat): the mode (where Brim is used), how much Brim does, what to use it for, sensitivity, how often, what
// happens, voice. The hat's settings (stored on the hat, shared with the web app) save as you change
// them; the mode, scale, speech and vibration are this phone's own.
import { useEffect, useRef, useState } from "react";
import { AccessibilityInfo, Pressable, ScrollView, StyleSheet, Text, View } from "react-native";
import type { Settings } from "@schema";
import { errorText } from "../api";
import { useBrim, type Level } from "../BrimContext";
import { Bunny, Kicker, Scale, Seg, ToggleRow } from "../design";
import { T, tight } from "../theme";
import { MODES, modeOf, type Mode } from "../modes";
import { useFocusTitle } from "../ui";
import { say } from "../voice";

const LEVELS = ["Detect only", "Warn", "Warn and guide"] as const;
const LEVEL_TEXT = [
  "Brim notices and shows things here, but stays quiet.",
  "Brim speaks up when something needs your attention right away.",
  "Brim warns you, then says which way is clear.",
] as const;

// Closest preset for values set elsewhere (e.g. 1.1 from the web app).
const nearest = <V extends number>(opts: readonly (readonly [V, string])[], v: number): V =>
  opts.reduce((b, o) => (Math.abs(o[0] - v) < Math.abs(b[0] - v) ? o : b))[0];
const SPEEDS = [[0.85, "Slow"], [1, "Normal"], [1.25, "Fast"]] as const;
const VOLUMES = [[40, "Quiet"], [70, "Normal"], [100, "Loud"]] as const;
const SPACING = [[10, "10 s"], [30, "30 s"], [60, "1 min"]] as const;

export type SubPage = "alerts" | "activity" | "health";

export function SettingsScreen({ onDone, open }: { onDone: () => void; open: (p: SubPage) => void }) {
  const { api, applySettings, hatUrl, forgetHat, source, level, setLevel, speaking, setSpeaking, vibrate, setVibrate, unreadAlerts, mode, setMode } = useBrim();
  const title = useFocusTitle();
  const [s, setS] = useState<Settings | null>(null);
  const [error, setError] = useState<string | null>(null);
  const [saved, setSaved] = useState("");
  const timer = useRef<ReturnType<typeof setTimeout> | null>(null);

  useEffect(() => {
    if (!api) return;
    api.settings().then(setS, (e: unknown) => setError(errorText(e, "Couldn't load settings.")));
  }, [api]);
  useEffect(() => () => {
    if (timer.current) clearTimeout(timer.current);
  }, []);

  // Save shortly after each change (no Save button), then apply the voice parts to this phone.
  function update<K extends keyof Settings>(k: K, v: Settings[K]) {
    if (!s || !api) return;
    const next = { ...s, [k]: v };
    setS(next);
    setSaved("");
    if (timer.current) clearTimeout(timer.current);
    timer.current = setTimeout(async () => {
      try {
        const out = await api.saveSettings(next);
        setS(out);
        applySettings(out);
        setError(null);
        setSaved(source === "camera" ? "Saved on this phone and sent to the camera." : "Saved to your hat.");
      } catch (e) {
        const m = errorText(e, "Couldn't save settings.");
        setError(m);
        AccessibilityInfo.announceForAccessibility(m);
      }
    }, 500);
  }

  return (
    <ScrollView style={{ backgroundColor: T.bg }} contentContainerStyle={st.wrap}>
      <View style={st.head}>
        <View style={{ flex: 1, gap: 6 }}>
          <Kicker>Preferences</Kicker>
          <Text ref={title} accessibilityRole="header" style={[st.h1, tight(32)]}>
            What should Brim do?
          </Text>
        </View>
        <Pressable accessibilityRole="button" accessibilityLabel="Done, back to Brim" onPress={onDone} style={st.done}>
          <Text style={st.doneText}>Done</Text>
        </Pressable>
      </View>

      <Label color={T.purple}>Mode</Label>
      <View accessibilityRole="radiogroup" accessibilityLabel="Mode" style={st.card}>
        {MODES.map((m, i) => (
          <ModeRow
            key={m.id}
            mode={m}
            on={m.id === mode}
            position={`${i + 1} of ${MODES.length}`}
            onPress={() => {
              if (m.id === mode) return;
              setMode(m.id);
              say(`${modeOf(m.id).name} mode.`, "polite");
            }}
          />
        ))}
      </View>
      <Text style={[st.note, { marginBottom: 26 }]}>Safety warnings and directions work the same in every mode.</Text>

      <View style={[st.card, { padding: 20, gap: 14 }]}>
        <Kicker>How much Brim does</Kicker>
        <Scale label="How much Brim does" stops={LEVELS} value={level} onChange={(v) => setLevel(v as Level)} />
        <Text style={st.help}>{LEVEL_TEXT[level]}</Text>
      </View>

      {error ? (
        <Text accessibilityLiveRegion="assertive" style={st.error}>
          {error}
        </Text>
      ) : null}
      {!s && !error ? <Text style={st.help}>Loading settings…</Text> : null}

      {s ? (
        <>
          <Label color={T.purple}>Use Brim for</Label>
          <View style={[st.card, { backgroundColor: "rgba(243,238,255,0.95)" }]}>
            <ToggleRow label="Person close ahead" locked value />
            <ToggleRow label="Phone pointed at you" locked value />
            <ToggleRow label="Obstacles and which side" sub="“Chair ahead, step right”" value={s.announce_obstacles} onChange={(v) => update("announce_obstacles", v)} />
            <ToggleRow label="Where people are" sub="Left, ahead or right. Never who." value={s.announce_people} onChange={(v) => update("announce_people", v)} />
          </View>

          <Label>Sensitivity</Label>
          <Seg label="Safety warning sensitivity" value={s.safety_sensitivity} onChange={(v) => update("safety_sensitivity", v)} options={[["low", "Low"], ["normal", "Normal"], ["high", "High"]]} />
          <Text style={st.note}>{s.safety_sensitivity === "low" ? "Only when something is very close." : s.safety_sensitivity === "high" ? "Warns early, more often." : "Balanced for most walks."}</Text>

          <Label>Warn me after</Label>
          <Seg label="Time between announcements about people and obstacles" value={nearest(SPACING, s.info_min_interval_s)} onChange={(v) => update("info_min_interval_s", v)} options={SPACING} />
          <Text style={st.note}>Between mentions of people and obstacles. Safety warnings and directions never wait.</Text>
        </>
      ) : null}

      <Label>When it happens</Label>
      <View style={st.card}>
        <ToggleRow dark label="Warn me out loud" sub="Keeps speaking with the phone locked" value={speaking} onChange={setSpeaking} />
        <ToggleRow dark label="Vibrate on safety warnings" sub="A double buzz for danger" value={vibrate} onChange={setVibrate} />
      </View>

      {s ? (
        <>
          <Label>Voice</Label>
          <Seg label="Speaking speed" value={nearest(SPEEDS, s.speech_rate)} onChange={(v) => update("speech_rate", v)} options={SPEEDS} />
          <View style={{ height: 10 }} />
          <Seg label="Volume" value={nearest(VOLUMES, s.volume)} onChange={(v) => update("volume", v)} options={VOLUMES} />
          <Text style={st.note}>Safety warnings are never quieter than half volume.</Text>
        </>
      ) : null}
      {saved ? (
        <Text accessibilityLiveRegion="polite" style={st.saved}>
          {saved}
        </Text>
      ) : null}

      {source === "hat" ? (
        <>
          <Label>More</Label>
          <View style={st.card}>
            <LinkRow label="Alerts" value={unreadAlerts ? `${unreadAlerts} new` : undefined} onPress={() => open("alerts")} />
            <LinkRow label="Activity" onPress={() => open("activity")} />
            <LinkRow label="Hat health" onPress={() => open("health")} />
          </View>
        </>
      ) : null}

      <Label>{source === "camera" ? "Laptop camera" : "Your hat"}</Label>
      <View style={st.card}>
        <Text style={[st.help, { padding: 16 }]}>
          {source === "camera" ? "Connected to the camera page at " : "Paired with "}
          {hatUrl?.replace(/^https?:\/\//, "") ?? "nothing yet"}.
        </Text>
        <LinkRow
          label={source === "camera" ? "Disconnect the camera" : "Forget this hat"}
          hint={source === "camera" ? "Go back to connecting a hat or camera." : "Stops listening to this hat. You will need its PIN to pair again."}
          danger
          onPress={() => void forgetHat()}
        />
      </View>
    </ScrollView>
  );
}

function Label({ children, color = T.dark }: { children: string; color?: string }) {
  return (
    <Text accessibilityRole="header" style={[st.label, { color }]}>
      {children.toUpperCase()}
    </Text>
  );
}

function ModeRow({ mode, on, position, onPress }: { mode: Mode; on: boolean; position: string; onPress: () => void }) {
  return (
    <Pressable
      accessibilityRole="radio"
      accessibilityLabel={mode.name}
      accessibilityHint={`${mode.blurb} Mode, ${position}.`}
      accessibilityState={{ selected: on, checked: on }}
      onPress={onPress}
      style={[st.mode, on && { backgroundColor: mode.id === "everyday" ? T.tint : mode.wash }]}
    >
      <Bunny name={mode.bunny} size={40} />
      <View style={{ flex: 1, gap: 2 }}>
        <Text style={[st.linkText, { color: mode.color }, on && { fontWeight: "800" }]}>{mode.name}</Text>
        <Text style={st.modeSub}>{mode.blurb}</Text>
      </View>
      <View style={[st.radio, on && { borderColor: mode.color }]}>{on ? <View style={[st.radioDot, { backgroundColor: mode.color }]} /> : null}</View>
    </Pressable>
  );
}

function LinkRow({ label, value, hint, onPress, danger }: { label: string; value?: string; hint?: string; onPress: () => void; danger?: boolean }) {
  return (
    <Pressable accessibilityRole="button" accessibilityLabel={value ? `${label}, ${value}` : label} accessibilityHint={hint} onPress={onPress} style={st.link}>
      <Text style={[st.linkText, danger && { color: T.red }]}>{label}</Text>
      {value ? <Text style={st.linkValue}>{value}</Text> : null}
      {!danger ? <Text style={st.chev}>›</Text> : null}
    </Pressable>
  );
}

const st = StyleSheet.create({
  wrap: { paddingHorizontal: 20, paddingTop: 72, paddingBottom: 48 },
  head: { flexDirection: "row", alignItems: "flex-start", gap: 12, marginBottom: 26 },
  h1: { color: T.ink, fontWeight: "700", lineHeight: 38 },
  done: { minHeight: 44, paddingHorizontal: 18, borderRadius: 999, backgroundColor: T.dark, justifyContent: "center" },
  doneText: { color: "#fff", fontSize: 15, fontWeight: "700" },
  card: { borderRadius: 22, backgroundColor: "rgba(255,255,255,0.85)", paddingVertical: 4 },
  help: { color: T.muted, fontSize: 14, lineHeight: 20 },
  note: { color: T.muted, fontSize: 12, lineHeight: 17, marginTop: 8, marginHorizontal: 6 },
  label: { fontSize: 12, fontWeight: "800", letterSpacing: 0.5, marginTop: 26, marginBottom: 10, marginHorizontal: 6 },
  error: { color: T.red, fontSize: 15, fontWeight: "700", marginTop: 14 },
  saved: { color: T.green, fontSize: 14, fontWeight: "700", marginTop: 14, marginHorizontal: 6 },
  link: { flexDirection: "row", alignItems: "center", minHeight: 52, paddingHorizontal: 16, gap: 8 },
  linkText: { flex: 1, color: T.ink, fontSize: 15, fontWeight: "600" },
  linkValue: { color: T.red, fontSize: 13, fontWeight: "700" },
  chev: { color: T.muted, fontSize: 22 },
  mode: { flexDirection: "row", alignItems: "center", minHeight: 64, paddingHorizontal: 12, paddingVertical: 8, gap: 12, borderRadius: 18 },
  modeSub: { color: T.muted, fontSize: 12, lineHeight: 16 },
  radio: { width: 24, height: 24, borderRadius: 12, borderWidth: 2, borderColor: T.lavender, alignItems: "center", justifyContent: "center" },
  radioDot: { width: 12, height: 12, borderRadius: 6, backgroundColor: T.dark },
});
