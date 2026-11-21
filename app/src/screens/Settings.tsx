import { useEffect, useState, type FormEvent } from "react";
import { SENSITIVITY_VALUES, type Sensitivity, type Settings } from "@schema";
import { api } from "../api/client";
import { ErrorMessage, Loading, Screen } from "./Screen";

const SENSITIVITY_LABEL: Record<Sensitivity, string> = {
  low: "Low – only when something is very close",
  normal: "Normal",
  high: "High – warn early, more often",
};

export function SettingsScreen() {
  const [s, setS] = useState<Settings | null>(null);
  const [error, setError] = useState<string | null>(null);
  const [saved, setSaved] = useState("");
  const [busy, setBusy] = useState(false);

  useEffect(() => {
    api.settings().then(setS, (e: unknown) => setError(e instanceof Error ? e.message : "Couldn't load settings."));
  }, []);

  function update<K extends keyof Settings>(k: K, v: Settings[K]) {
    setS((cur) => (cur ? { ...cur, [k]: v } : cur));
    setSaved("");
  }

  async function submit(e: FormEvent) {
    e.preventDefault();
    if (!s) return;
    setBusy(true);
    setError(null);
    try {
      setS(await api.saveSettings(s));
      setSaved("Settings saved to the hat.");
    } catch (err) {
      setError(err instanceof Error ? err.message : "Couldn't save settings.");
    } finally {
      setBusy(false);
    }
  }

  return (
    <Screen title="Settings">
      <ErrorMessage error={error} />
      {!s && !error && <Loading what="settings" />}
      {s && (
        <form onSubmit={submit} className="settings">
          <label htmlFor="volume">Voice volume</label>
          <input
            id="volume"
            type="range"
            min={0}
            max={100}
            step={5}
            value={s.volume}
            aria-valuetext={`${s.volume} percent`}
            onChange={(e) => update("volume", Number(e.target.value))}
          />
          <p className="muted" aria-hidden="true">
            {s.volume}%
          </p>

          <label htmlFor="rate">Speaking speed</label>
          <select id="rate" value={String(s.speech_rate)} onChange={(e) => update("speech_rate", Number(e.target.value))}>
            <option value="0.75">Slower</option>
            <option value="1">Normal</option>
            <option value="1.25">Faster</option>
            <option value="1.5">Fastest</option>
          </select>

          <div className="check">
            <input
              id="people"
              type="checkbox"
              checked={s.announce_people}
              onChange={(e) => update("announce_people", e.target.checked)}
            />
            <label htmlFor="people">Tell me where people are</label>
          </div>

          <div className="check">
            <input
              id="obstacles"
              type="checkbox"
              checked={s.announce_obstacles}
              onChange={(e) => update("announce_obstacles", e.target.checked)}
            />
            <label htmlFor="obstacles">Name obstacles and which side they are on</label>
          </div>

          <label htmlFor="interval">Minimum seconds between these announcements</label>
          <input
            id="interval"
            type="number"
            inputMode="numeric"
            min={10}
            max={600}
            value={s.info_min_interval_s}
            aria-describedby="interval-help"
            onChange={(e) => update("info_min_interval_s", Number(e.target.value))}
          />
          <p id="interval-help" className="muted">
            At least 10. Safety warnings and directions are never delayed by this.
          </p>

          <fieldset>
            <legend>Safety warning sensitivity</legend>
            {SENSITIVITY_VALUES.map((v) => (
              <div className="check" key={v}>
                <input
                  id={`sens-${v}`}
                  type="radio"
                  name="sensitivity"
                  value={v}
                  checked={s.safety_sensitivity === v}
                  onChange={() => update("safety_sensitivity", v)}
                />
                <label htmlFor={`sens-${v}`}>{SENSITIVITY_LABEL[v]}</label>
              </div>
            ))}
          </fieldset>

          <button type="submit" className="primary" disabled={busy}>
            {busy ? "Saving…" : "Save settings"}
          </button>
          <p role="status" className="ok">
            {saved}
          </p>
        </form>
      )}
    </Screen>
  );
}
