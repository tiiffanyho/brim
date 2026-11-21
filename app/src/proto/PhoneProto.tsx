// Phone prototype (dev only, #/phone): the Figma onboarding (O1–O4), the Live screen (D1–D5) and
// Settings, working. Live runs from the laptop camera (#/camera on the same Wi-Fi) or from the
// built-in demo walk; Settings change what Brim says and are sent to the camera.
import { useCallback, useEffect, useMemo, useRef, useState, type ReactNode } from "react";
import type { BrimEvent } from "@schema";
import { openLink, type Link } from "../vision/link";
import { bunnySvg, type BunnyName } from "./bunnies";
import { Glows } from "./Glows";
import {
  cameraScreen, demoFor, emptyCamera, LEVEL_TEXT, loadSettings, onCameraEvent, RATE, saveSettings,
  type CameraView, type ProtoSettings, type Screen,
} from "./model";
import { canListen, listenOnce, PhoneVoice } from "./voice";
import "./proto.css";

type View = "onboarding" | "live" | "settings";
const ONBOARDED = "brim-proto-onboarded";
const seen = () => {
  try {
    return localStorage.getItem(ONBOARDED) === "1";
  } catch {
    return false;
  }
};

function Bunny({ name, className }: { name: BunnyName; className?: string }) {
  return <span className={`pr-bunny ${className ?? ""}`} aria-hidden="true" dangerouslySetInnerHTML={{ __html: bunnySvg(name) }} />;
}

function VoiceBar({ hint, listening, onPress }: { hint: string; listening: boolean; onPress: () => void }) {
  return (
    <button className={`pr-voice ${listening ? "on" : ""}`} onClick={onPress} aria-label={listening ? "Listening" : `Voice. ${hint}`}>
      <span className="pr-mic" aria-hidden="true">
        <svg width="18" height="18" viewBox="0 0 24 24" fill="none">
          <rect x="9" y="3" width="6" height="11" rx="3" stroke="currentColor" strokeWidth="2" />
          <path d="M5 11a7 7 0 0 0 14 0M12 18v3" stroke="currentColor" strokeWidth="2" strokeLinecap="round" />
        </svg>
      </span>
      <span className="pr-voice-text">{listening ? "Listening…" : hint}</span>
      <span className="pr-voice-dots" aria-hidden="true">
        {[6, 12, 8, 14, 6].map((h, i) => (
          <i key={i} style={{ height: h }} />
        ))}
      </span>
    </button>
  );
}

export function PhoneProtoScreen() {
  const [settings, setSettings] = useState<ProtoSettings>(loadSettings);
  const [view, setView] = useState<View>(() => (seen() ? "live" : "onboarding"));
  const [step, setStep] = useState(0);
  const [voiceOn, setVoiceOn] = useState(false);
  const [listening, setListening] = useState(false);
  const [toast, setToast] = useState("");
  const [cam, setCam] = useState<CameraView>(emptyCamera);
  const [camLive, setCamLive] = useState(false);
  const [demoIdx, setDemoIdx] = useState(0);
  const [now, setNow] = useState(Date.now());
  const voice = useRef(new PhoneVoice());
  const link = useRef<Link | null>(null);
  const settingsRef = useRef(settings);
  const sourceChosen = useRef(false);
  const lastCamAt = useRef(0);

  // ---- settings: save, apply to the voice, and send the camera what it needs ----
  useEffect(() => {
    settingsRef.current = settings;
    saveSettings(settings);
    voice.current.rate = RATE[settings.voice];
    link.current?.send({
      type: "proto_settings",
      sensitivity: settings.sensitivity,
      hazards: settings.use.hazards,
      people: settings.use.people,
    });
  }, [settings]);
  const update = (patch: Partial<ProtoSettings>) => setSettings((s) => ({ ...s, ...patch }));

  /** What gets spoken depends on the scale: detect only = nothing, warn = warnings only. */
  const speak = useCallback((text: string, kind: "alert" | "nav" | "status") => {
    const s = settingsRef.current;
    if (!s.when.speak || s.level === 0) return;
    if (s.level === 1 && kind === "nav") return;
    if (kind === "alert") {
      voice.current.alert(text);
      navigator.vibrate?.([200, 80, 200]);
    } else voice.current.nav(text);
  }, []);

  // ---- laptop camera link ----
  useEffect(() => {
    const v = voice.current;
    link.current = openLink(
      "phone",
      (m) => {
        if (m.type === "peers" || m.type === "proto_settings") return;
        const e = m as BrimEvent;
        lastCamAt.current = Date.now();
        setCamLive(true);
        if (!sourceChosen.current && settingsRef.current.source !== "camera") update({ source: "camera" });
        setCam((v) => onCameraEvent(v, e));
        if (settingsRef.current.source !== "camera") return;
        if (e.type === "safety_alert") speak(e.text, "alert");
        else if (e.type === "clear_path") speak(e.text, "nav");
      },
      () => {},
    );
    const t = setInterval(() => {
      setNow(Date.now());
      setCamLive(Date.now() - lastCamAt.current < 5000);
    }, 500);
    return () => {
      clearInterval(t);
      link.current?.close();
      v.stop();
    };
  }, [speak]);

  // ---- demo walk (D1–D5) ----
  const steps = useMemo(() => demoFor(settings), [settings]);
  const demoRunning = view === "live" && settings.source === "demo";
  useEffect(() => {
    if (!demoRunning) return;
    const st = steps[demoIdx % steps.length]!;
    const scr = st.screen(settingsRef.current);
    speak(scr.quote, st.speak);
    const t = setTimeout(() => setDemoIdx((i) => (i + 1) % steps.length), st.ms);
    return () => clearTimeout(t);
  }, [demoRunning, demoIdx, steps, speak]);

  const screen: Screen =
    settings.source === "camera" ? cameraScreen(cam, now) : steps[demoIdx % steps.length]!.screen(settings);

  const unlockVoice = () => {
    voice.current.enabled = true;
    setVoiceOn(true);
  };

  const showToast = (t: string) => {
    setToast(t);
    setTimeout(() => setToast(""), 3500);
  };

  // ---- voice bar: listen for a few commands; without speech recognition, a tap repeats ----
  const onVoice = async () => {
    unlockVoice();
    if (!canListen()) {
      voice.current.now(screen.quote);
      return;
    }
    voice.current.stop();
    setListening(true);
    const heard = (await listenOnce())?.toLowerCase() ?? "";
    setListening(false);
    const s = settingsRef.current;
    if (/around|what.*(see|ahead)/.test(heard)) voice.current.now(screen.rows.map(([k, v]) => `${k}: ${v}`).join(". "));
    else if (/setting/.test(heard)) setView("settings");
    else if (/911|help/.test(heard)) showToast("Calling 911. Prototype: no real call is made.");
    else if (/call/.test(heard)) showToast(`Calling ${s.contact}… Prototype: no real call is made.`);
    else if (/okay|ok|fine|safe/.test(heard)) {
      voice.current.now("Glad you're okay.");
      setDemoIdx(0);
    } else voice.current.now(screen.quote); // "repeat", or anything we didn't catch
  };

  /* ---------------- Onboarding ---------------- */
  if (view === "onboarding") {
    const finish = () => {
      try {
        localStorage.setItem(ONBOARDED, "1");
      } catch {
        /* ok */
      }
      unlockVoice();
      setDemoIdx(0);
      setView("live");
    };
    return (
      <Onboarding
        step={step}
        setStep={setStep}
        voice={voice.current}
        unlock={unlockVoice}
        finish={finish}
        onVoice={onVoice}
        listening={listening}
      />
    );
  }

  /* ---------------- Settings ---------------- */
  if (view === "settings") {
    return (
      <SettingsView
        s={settings}
        update={update}
        camLive={camLive}
        onDone={() => setView("live")}
        chooseSource={(source) => {
          sourceChosen.current = true;
          setDemoIdx(0);
          update({ source });
        }}
        restart={() => {
          try {
            localStorage.removeItem(ONBOARDED);
          } catch {
            /* ok */
          }
          setStep(0);
          setView("onboarding");
        }}
      />
    );
  }

  /* ---------------- Live (D1–D5) ---------------- */
  const bunny: BunnyName = "brim";
  return (
    <div className={`pr pr-live tone-${screen.tone}`}>
      <Glows tone={screen.tone} seed={settings.source === "camera" ? 11 : 7 + (demoIdx % 5) * 3} />
      <header className="pr-live-top">
        <span className="pr-source">
          {settings.source === "camera" ? (camLive ? "Laptop camera" : "Waiting for the laptop camera…") : "Demo walk"}
        </span>
        <button className="pr-round" onClick={() => setView("settings")} aria-label="Settings">
          <svg width="22" height="22" viewBox="0 0 24 24" fill="none" aria-hidden="true">
            <path d="M4 7h10M18 7h2M4 17h4M12 17h8" stroke="currentColor" strokeWidth="2" strokeLinecap="round" />
            <circle cx="16" cy="7" r="2.2" stroke="currentColor" strokeWidth="2" />
            <circle cx="10" cy="17" r="2.2" stroke="currentColor" strokeWidth="2" />
          </svg>
        </button>
      </header>
      <main className="pr-live-main" id="main">
        <div className="pr-wave" aria-hidden="true">
          {[10, 20, 32, 16, 26, 36, 18, 28, 12, 22, 14].map((h, i) => (
            <i key={i} style={{ height: h, animationDelay: `${i * 0.08}s` }} />
          ))}
        </div>
        <h1 className="visually-hidden">Brim says</h1>
        <p className="pr-quote" aria-live={screen.tone === "alert" ? "assertive" : "polite"}>
          {screen.quote}
        </p>
        <Bunny name={bunny} className="pr-live-bunny" />
        <dl className="pr-rows">
          {screen.rows.map(([k, v, hl]) => (
            <div key={k}>
              <dt>{k}</dt>
              <dd className={hl ?? ""}>{v}</dd>
            </div>
          ))}
        </dl>
        {screen.next && (
          <div className="pr-next">
            <span>Next step</span>
            <p>{screen.next}</p>
          </div>
        )}
      </main>
      <footer className="pr-foot">
        <VoiceBar hint={screen.hint} listening={listening} onPress={onVoice} />
        {!voiceOn && <p className="pr-tiny">Tap the bar once to turn on Brim's voice.</p>}
      </footer>
      <p className={`pr-toast ${toast ? "show" : ""}`} role="status">
        {toast}
      </p>
    </div>
  );
}

/* ================= Onboarding (O1–O4) ================= */

function Dots({ i }: { i: number }) {
  return (
    <div className="pr-dots" aria-label={`Step ${i + 1} of 4`} role="img">
      {[0, 1, 2, 3].map((k) => (
        <i key={k} className={k === i ? "on" : ""} />
      ))}
    </div>
  );
}

function Onboarding({
  step, setStep, voice, unlock, finish, onVoice, listening,
}: {
  step: number;
  setStep: (n: number) => void;
  voice: PhoneVoice;
  unlock: () => void;
  finish: () => void;
  onVoice: () => void;
  listening: boolean;
}) {
  const h1 = useRef<HTMLHeadingElement>(null);
  const [digits, setDigits] = useState(["", "", "", "", "", ""]);
  const refs = useRef<(HTMLInputElement | null)[]>([]);
  useEffect(() => h1.current?.focus(), [step]);
  const test = () => {
    unlock();
    voice.now("Chair ahead, step right.");
  };

  const shell = (bunny: BunnyName, title: string, body: string, extra: ReactNode, actions: ReactNode, hint: string) => (
    <div className="pr pr-onb">
      <Glows tone="calm" seed={20 + step * 5} />
      <Dots i={step} />
      <Bunny name={bunny} className="pr-onb-bunny" />
      <h1 ref={h1} tabIndex={-1}>
        {title}
      </h1>
      <p className="pr-lead">{body}</p>
      {extra}
      <div className="pr-grow" />
      <div className="pr-actions">{actions}</div>
      <VoiceBar hint={hint} listening={listening} onPress={onVoice} />
    </div>
  );

  if (step === 0)
    return shell(
      "brim",
      "Hi, I'm Brim.",
      "I sit on your hat and tell you what is in front of you, so you can walk with confidence.",
      null,
      <button className="pr-btn" onClick={() => (unlock(), setStep(1))}>
        Get started
      </button>,
      'Or say "Get started"',
    );
  if (step === 1) {
    const pin = digits.join("");
    const set = (i: number, v: string) => {
      const c = v.replace(/\D/g, "");
      if (c.length > 1) {
        const all = c.slice(0, 6).split("");
        setDigits([...all, ...Array(6 - all.length).fill("")]);
        return;
      }
      const n = [...digits];
      n[i] = c;
      setDigits(n);
      if (c && i < 5) refs.current[i + 1]?.focus();
    };
    return shell(
      "wave",
      "Turn on your hat",
      "Brim will say a 6-digit code in your ear. Type it here.",
      <fieldset className="pr-pin">
        <legend className="visually-hidden">Pairing code</legend>
        {digits.map((d, i) => (
          <input
            key={i}
            ref={(el) => {
              refs.current[i] = el;
            }}
            inputMode="numeric"
            autoComplete={i === 0 ? "one-time-code" : "off"}
            aria-label={`Digit ${i + 1} of 6`}
            value={d}
            onChange={(e) => set(i, e.target.value)}
            onKeyDown={(e) => {
              if (e.key === "Backspace" && !d && i > 0) refs.current[i - 1]?.focus();
            }}
          />
        ))}
      </fieldset>,
      <>
        <button className="pr-btn" disabled={pin.length !== 6} onClick={() => (voice.now("Connected."), setStep(2))}>
          Connect
        </button>
        <button className="pr-btn glass" onClick={() => setStep(2)}>
          No hat yet? Use the demo
        </button>
      </>,
      'Say "Repeat the code"',
    );
  }
  if (step === 2)
    return shell(
      "curious",
      "Can you hear me?",
      "Put in your earpiece, then play a test.",
      <button className="pr-test" onClick={test}>
        <span className="pr-wave small" aria-hidden="true">
          {[8, 16, 24, 12, 20, 10].map((h, i) => (
            <i key={i} style={{ height: h }} />
          ))}
        </span>
        <span>"Chair ahead, step right."</span>
      </button>,
      <>
        <button className="pr-btn" onClick={() => setStep(3)}>
          I heard it
        </button>
        <button className="pr-btn glass" onClick={test}>
          Play the test again
        </button>
      </>,
      'Say "I heard it"',
    );
  return shell(
    "hop",
    "You're all set",
    "Walk forward slowly. I will tell you when something is in the way, and which way is clear.",
    null,
    <button className="pr-btn" onClick={finish}>
      Start walking
    </button>,
    'Say "Start walking"',
  );
}

/* ================= Settings (flat) ================= */

function Toggle({ label, sub, on, set, dark }: { label: string; sub?: string; on: boolean; set: (v: boolean) => void; dark?: boolean }) {
  return (
    <button className={`pr-row ${dark ? "dark" : ""}`} role="switch" aria-checked={on} onClick={() => set(!on)}>
      <span className="pr-row-text">
        <b>{label}</b>
        {sub && <small>{sub}</small>}
      </span>
      <span className="pr-switch" aria-hidden="true" />
    </button>
  );
}

function Seg<T extends string | number>({ label, value, options, set }: { label: string; value: T; options: [T, string][]; set: (v: T) => void }) {
  return (
    <div className="pr-seg" role="radiogroup" aria-label={label}>
      {options.map(([v, l]) => (
        <button key={String(v)} role="radio" aria-checked={v === value} onClick={() => set(v)}>
          {l}
        </button>
      ))}
    </div>
  );
}

function SettingsView({
  s, update, onDone, chooseSource, restart, camLive,
}: {
  s: ProtoSettings;
  update: (p: Partial<ProtoSettings>) => void;
  onDone: () => void;
  chooseSource: (v: "demo" | "camera") => void;
  restart: () => void;
  camLive: boolean;
}) {
  const h1 = useRef<HTMLHeadingElement>(null);
  useEffect(() => h1.current?.focus(), []);
  const use = (k: keyof ProtoSettings["use"]) => (v: boolean) => update({ use: { ...s.use, [k]: v } });
  const when = (k: keyof ProtoSettings["when"]) => (v: boolean) => update({ when: { ...s.when, [k]: v } });
  const LABELS = ["Detect only", "Warn", "Warn and guide"];
  return (
    <div className="pr pr-settings">
      <header className="pr-set-head">
        <div>
          <p className="pr-kicker">Preferences</p>
          <h1 ref={h1} tabIndex={-1}>
            What should Brim do?
          </h1>
        </div>
        <button className="pr-done" onClick={onDone}>
          Done
        </button>
      </header>

      <section className="pr-card pr-scale" aria-labelledby="scale-l">
        <p className="pr-kicker" id="scale-l">
          How much Brim does
        </p>
        <input
          type="range"
          min={0}
          max={2}
          step={1}
          value={s.level}
          aria-labelledby="scale-l"
          aria-valuetext={LABELS[s.level]}
          onChange={(e) => update({ level: Number(e.target.value) as 0 | 1 | 2 })}
          style={{ ["--pct" as string]: `${s.level * 50}%` }}
        />
        <div className="pr-scale-labels" aria-hidden="true">
          {LABELS.map((l, i) => (
            <span key={l} className={i === s.level ? "on" : ""}>
              {l}
            </span>
          ))}
        </div>
        <p className="pr-help">{LEVEL_TEXT[s.level]}</p>
      </section>

      <h2 className="pr-label purple">Use Brim for</h2>
      <div className="pr-card tint">
        <Toggle label="Someone following" on={s.use.following} set={use("following")} />
        <Toggle label="Phone pointed at you" on={s.use.phone} set={use("phone")} />
        <Toggle label="Hazards and accidents" on={s.use.hazards} set={use("hazards")} />
        <Toggle label="Route changes" on={s.use.routes} set={use("routes")} />
        <Toggle label="Where people are" sub="Left, ahead or right. Never who." on={s.use.people} set={use("people")} />
      </div>

      <h2 className="pr-label">Sensitivity</h2>
      <Seg label="Sensitivity" value={s.sensitivity} set={(v) => update({ sensitivity: v })} options={[["low", "Low"], ["normal", "Normal"], ["high", "High"]]} />
      <h2 className="pr-label">Warn me after</h2>
      <Seg label="Warn me after" value={s.warnAfterMin} set={(v) => update({ warnAfterMin: v })} options={[[3, "3 min"], [5, "5 min"], [10, "10 min"]]} />
      <h2 className="pr-label">When it happens</h2>
      <div className="pr-card">
        <Toggle dark label="Warn me out loud" sub="Brim speaks right away" on={s.when.speak} set={when("speak")} />
        <Toggle dark label="Find the next safest place" sub="Open, staffed, well-lit places nearby" on={s.when.safePlace} set={when("safePlace")} />
        <Toggle dark label="Share my location" sub={`With ${s.contact}, until you say you are safe`} on={s.when.share} set={when("share")} />
        <Toggle dark label="Save a description" sub="Clothing and behaviour only, deleted after 7 days" on={s.when.describe} set={when("describe")} />
      </div>
      <h2 className="pr-label">Voice</h2>
      <Seg label="Voice speed" value={s.voice} set={(v) => update({ voice: v })} options={[["slow", "Slow"], ["normal", "Normal"], ["fast", "Fast"]]} />

      <h2 className="pr-label muted">Prototype</h2>
      <Seg
        label="What Brim watches"
        value={s.source}
        set={chooseSource}
        options={[["demo", "Demo walk"], ["camera", camLive ? "Laptop camera ●" : "Laptop camera"]]}
      />
      <div className="pr-card">
        <label className="pr-field">
          <span>Trusted contact</span>
          <input value={s.contact} onChange={(e) => update({ contact: e.target.value || "Mom" })} autoComplete="off" />
        </label>
      </div>
      <button className="pr-link" onClick={restart}>
        Show onboarding again
      </button>
    </div>
  );
}
