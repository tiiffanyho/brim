// The hat's settings (stored on the hat, shared with the web app) plus this phone's own options.
import { useEffect, useState } from "react";
import { AccessibilityInfo, Text } from "react-native";
import { SENSITIVITY_VALUES, type Sensitivity, type Settings } from "@schema";
import { errorText } from "../api";
import { useBrim } from "../BrimContext";
import { Body, Button, Choice, ErrorText, H2, Loading, Screen, Stepper, SwitchRow, styles, useColors } from "../ui";

const SENSITIVITY_LABEL: Record<Sensitivity, string> = {
  low: "Low: only when something is very close",
  normal: "Normal",
  high: "High: warn early, more often",
};

const RATES = [
  { value: 0.75, label: "Slower" },
  { value: 1, label: "Normal" },
  { value: 1.25, label: "Faster" },
  { value: 1.5, label: "Fastest" },
] as const;

export function SettingsScreen() {
  const c = useColors();
  const { api, applySettings, hatUrl, forgetHat } = useBrim();
  const [s, setS] = useState<Settings | null>(null);
  const [error, setError] = useState<string | null>(null);
  const [saved, setSaved] = useState("");
  const [busy, setBusy] = useState(false);

  useEffect(() => {
    if (!api) return;
    api.settings().then(setS, (e: unknown) => setError(errorText(e, "Couldn't load settings.")));
  }, [api]);

  function update<K extends keyof Settings>(k: K, v: Settings[K]) {
    setS((cur) => (cur ? { ...cur, [k]: v } : cur));
    setSaved("");
  }

  async function save() {
    if (!s || !api) return;
    setBusy(true);
    setError(null);
    try {
      const out = await api.saveSettings(s);
      setS(out);
      applySettings(out);
      setSaved("Settings saved to the hat.");
      AccessibilityInfo.announceForAccessibility("Settings saved.");
    } catch (e) {
      const m = errorText(e, "Couldn't save settings.");
      setError(m);
      AccessibilityInfo.announceForAccessibility(m);
    } finally {
      setBusy(false);
    }
  }

  // Closest preset for rates set elsewhere (e.g. 1.1 from the web app).
  const rate = s ? RATES.reduce((best, r) => (Math.abs(r.value - s.speech_rate) < Math.abs(best.value - s.speech_rate) ? r : best)).value : 1;

  return (
    <Screen title="Settings">
      <ErrorText error={error} />
      {!s && !error ? <Loading what="settings" /> : null}
      {s ? (
        <>
          <H2>Voice</H2>
          <Stepper
            label="Voice volume"
            value={s.volume}
            min={0}
            max={100}
            step={10}
            format={(v) => `${v} percent`}
            onChange={(v) => update("volume", v)}
          />
          <Body muted>Safety warnings are never quieter than half volume.</Body>
          <Choice label="Speaking speed" options={RATES} value={rate} onChange={(v) => update("speech_rate", v)} />

          <H2>What to say</H2>
          <SwitchRow label="Tell me where people are" value={s.announce_people} onChange={(v) => update("announce_people", v)} />
          <SwitchRow
            label="Name obstacles and which side they are on"
            value={s.announce_obstacles}
            onChange={(v) => update("announce_obstacles", v)}
          />
          <Stepper
            label="Minimum time between these announcements"
            hint="Safety warnings and directions are never delayed by this. Swipe up or down to change."
            value={s.info_min_interval_s}
            min={10}
            max={600}
            step={10}
            format={(v) => (v < 60 ? `${v} seconds` : `${Math.floor(v / 60)} minute${v >= 120 ? "s" : ""}${v % 60 ? ` ${v % 60} seconds` : ""}`)}
            onChange={(v) => update("info_min_interval_s", v)}
          />

          <Choice
            label="Safety warning sensitivity"
            options={SENSITIVITY_VALUES.map((v) => ({ value: v, label: SENSITIVITY_LABEL[v] }))}
            value={s.safety_sensitivity}
            onChange={(v) => update("safety_sensitivity", v)}
          />

          <Button label={busy ? "Saving…" : "Save settings"} onPress={() => void save()} disabled={busy} />
          {saved ? (
            <Text accessibilityLiveRegion="polite" style={[styles.body, styles.bold, { color: c.ok }]}>
              {saved}
            </Text>
          ) : null}
        </>
      ) : null}

      <H2>This phone</H2>
      <Body muted>Paired with {hatUrl?.replace(/^https?:\/\//, "") ?? "no hat"}.</Body>
      <Button
        kind="secondary"
        label="Forget this hat"
        hint="Stops listening to this hat. You will need its PIN to pair again."
        onPress={() => void forgetHat()}
      />
    </Screen>
  );
}
