// Shared building blocks for every screen. VoiceOver first:
//   * every control has a role, a label and (where useful) a hint;
//   * values that go up and down are "adjustable" (swipe up / down with VoiceOver), not sliders;
//   * choices are radio buttons with their selected state; status is in words, never colour only;
//   * touch targets are at least 44 pt.
import { createContext, useContext, useEffect, useRef, type ComponentRef, type ReactNode } from "react";
import {
  AccessibilityInfo,
  ActivityIndicator,
  Pressable,
  ScrollView,
  StyleSheet,
  Switch,
  Text,
  View,
} from "react-native";

// The Brim palette (theme.ts), light only to match the redesigned screens. Contrast stays high:
// ink on the page is about 16:1, muted text about 6:1.
const COLORS = { bg: "#FAF8FF", fg: "#1B1530", muted: "#5E5873", card: "#F3EEFF", accent: "#6D28D9", danger: "#B91C1C", ok: "#15803D", border: "#DDD3F7" };
export function useColors() {
  return COLORS;
}

/** Set by the app shell for pages opened from Settings: shows "‹ Back" above the title. */
export const BackContext = createContext<{ label: string; onBack: () => void } | null>(null);
export type Colors = ReturnType<typeof useColors>;

/** Moves VoiceOver focus to the screen title when a screen appears. */
export function useFocusTitle() {
  const ref = useRef<ComponentRef<typeof Text>>(null);
  useEffect(() => {
    if (ref.current) AccessibilityInfo.sendAccessibilityEvent(ref.current, "focus");
  }, []);
  return ref;
}

/** Scrolling page with a focused h1 and an optional intro line. */
export function Screen({ title, intro, children }: { title: string; intro?: string; children: ReactNode }) {
  const c = useColors();
  const ref = useFocusTitle();
  const back = useContext(BackContext);
  return (
    <ScrollView style={{ backgroundColor: c.bg }} contentContainerStyle={styles.screen} keyboardShouldPersistTaps="handled">
      {back ? (
        <Pressable accessibilityRole="button" accessibilityLabel={`Back to ${back.label}`} onPress={back.onBack} style={styles.back} hitSlop={8}>
          <Text style={[styles.backText, { color: c.accent }]}>‹ {back.label}</Text>
        </Pressable>
      ) : null}
      <Text ref={ref} accessibilityRole="header" style={[styles.h1, { color: c.fg }]}>
        {title}
      </Text>
      {intro ? <Text style={[styles.body, { color: c.muted }]}>{intro}</Text> : null}
      {children}
    </ScrollView>
  );
}

export function H2({ children }: { children: string }) {
  const c = useColors();
  return (
    <Text accessibilityRole="header" style={[styles.h2, { color: c.fg }]}>
      {children}
    </Text>
  );
}

export function Body({ children, muted }: { children: ReactNode; muted?: boolean }) {
  const c = useColors();
  return <Text style={[styles.body, { color: muted ? c.muted : c.fg }]}>{children}</Text>;
}

export function Loading({ what }: { what: string }) {
  const c = useColors();
  return (
    <View style={styles.row} accessible accessibilityLabel={`Loading ${what}`}>
      <ActivityIndicator />
      <Text style={[styles.body, { color: c.muted, marginLeft: 10 }]}>Loading {what}…</Text>
    </View>
  );
}

export function ErrorText({ error }: { error: string | null }) {
  const c = useColors();
  if (!error) return null;
  return (
    <Text accessibilityLiveRegion="assertive" style={[styles.body, styles.bold, { color: c.danger }]}>
      {error}
    </Text>
  );
}

export function Button({ label, hint, onPress, disabled, kind = "primary" }: {
  label: string;
  hint?: string;
  onPress: () => void;
  disabled?: boolean;
  kind?: "primary" | "secondary";
}) {
  const c = useColors();
  const primary = kind === "primary";
  return (
    <Pressable
      accessibilityRole="button"
      accessibilityLabel={label}
      accessibilityHint={hint}
      accessibilityState={{ disabled: !!disabled }}
      disabled={disabled}
      onPress={onPress}
      style={({ pressed }) => [
        styles.button,
        { backgroundColor: primary ? c.accent : "transparent", borderColor: c.accent, opacity: disabled ? 0.5 : pressed ? 0.8 : 1 },
      ]}
    >
      <Text style={[styles.buttonText, { color: primary ? c.bg : c.accent }]}>{label}</Text>
    </Pressable>
  );
}

/** A labelled on/off switch; the whole row is one VoiceOver element. */
export function SwitchRow({ label, hint, value, onChange }: { label: string; hint?: string; value: boolean; onChange: (v: boolean) => void }) {
  const c = useColors();
  return (
    <Pressable
      accessibilityRole="switch"
      accessibilityLabel={label}
      accessibilityHint={hint}
      accessibilityState={{ checked: value }}
      onPress={() => onChange(!value)}
      style={[styles.row, styles.card, { backgroundColor: c.card, borderColor: c.border }]}
    >
      <Text style={[styles.body, styles.flex, { color: c.fg }]}>{label}</Text>
      <Switch value={value} onValueChange={onChange} importantForAccessibility="no" accessibilityElementsHidden />
    </Pressable>
  );
}

/**
 * A number with − and + buttons. For VoiceOver the row is one "adjustable" element: swipe up or
 * down to change it, and the new value is read out.
 */
export function Stepper({ label, hint, value, min, max, step, format, onChange }: {
  label: string;
  hint?: string;
  value: number;
  min: number;
  max: number;
  step: number;
  format: (v: number) => string;
  onChange: (v: number) => void;
}) {
  const c = useColors();
  const set = (v: number) => onChange(Math.min(max, Math.max(min, v)));
  return (
    <View
      accessible
      accessibilityRole="adjustable"
      accessibilityLabel={label}
      accessibilityHint={hint ?? "Swipe up or down to change."}
      accessibilityValue={{ text: format(value) }}
      accessibilityActions={[{ name: "increment" }, { name: "decrement" }]}
      onAccessibilityAction={(e) => set(value + (e.nativeEvent.actionName === "increment" ? step : -step))}
      style={[styles.card, { backgroundColor: c.card, borderColor: c.border }]}
    >
      <Text style={[styles.label, { color: c.fg }]}>{label}</Text>
      <View style={styles.row}>
        <Pressable onPress={() => set(value - step)} disabled={value <= min} style={[styles.stepBtn, { borderColor: c.accent, opacity: value <= min ? 0.4 : 1 }]}>
          <Text style={[styles.stepText, { color: c.accent }]}>−</Text>
        </Pressable>
        <Text style={[styles.value, { color: c.fg }]}>{format(value)}</Text>
        <Pressable onPress={() => set(value + step)} disabled={value >= max} style={[styles.stepBtn, { borderColor: c.accent, opacity: value >= max ? 0.4 : 1 }]}>
          <Text style={[styles.stepText, { color: c.accent }]}>+</Text>
        </Pressable>
      </View>
    </View>
  );
}

/** One-of-many choice as radio buttons. */
export function Choice<T extends string | number>({ label, options, value, onChange }: {
  label: string;
  options: readonly { value: T; label: string }[];
  value: T;
  onChange: (v: T) => void;
}) {
  const c = useColors();
  return (
    <View accessibilityRole="radiogroup" accessibilityLabel={label} style={styles.group}>
      <Text style={[styles.label, { color: c.fg }]}>{label}</Text>
      {options.map((o, i) => {
        const selected = o.value === value;
        return (
          <Pressable
            key={String(o.value)}
            accessibilityRole="radio"
            accessibilityLabel={o.label}
            accessibilityState={{ selected, checked: selected }}
            accessibilityHint={`${i + 1} of ${options.length}`}
            onPress={() => onChange(o.value)}
            style={[styles.row, styles.choice, { borderColor: selected ? c.accent : c.border, backgroundColor: c.card }]}
          >
            <Text style={[styles.radio, { color: selected ? c.accent : c.muted }]}>{selected ? "◉" : "○"}</Text>
            <Text style={[styles.body, styles.flex, { color: c.fg }, selected && styles.bold]}>{o.label}</Text>
          </Pressable>
        );
      })}
    </View>
  );
}

/** Label/value pairs, each read as one sentence ("Uptime: 12 minutes"). */
export function Facts({ rows }: { rows: readonly [string, string][] }) {
  const c = useColors();
  return (
    <View style={[styles.card, { backgroundColor: c.card, borderColor: c.border }]}>
      {rows.map(([k, v]) => (
        <View key={k} accessible accessibilityLabel={`${k}: ${v}`} style={styles.fact}>
          <Text style={[styles.factKey, { color: c.muted }]}>{k}</Text>
          <Text style={[styles.factVal, { color: c.fg }]}>{v}</Text>
        </View>
      ))}
    </View>
  );
}

export const styles = StyleSheet.create({
  screen: { padding: 20, paddingTop: 64, paddingBottom: 40, gap: 12 },
  back: { alignSelf: "flex-start", minHeight: 44, justifyContent: "center", marginTop: -16 },
  backText: { fontSize: 17, fontWeight: "600" },
  h1: { fontSize: 32, fontWeight: "700" },
  h2: { fontSize: 22, fontWeight: "700", marginTop: 12 },
  body: { fontSize: 18, lineHeight: 26 },
  small: { fontSize: 15 },
  bold: { fontWeight: "700" },
  label: { fontSize: 18, fontWeight: "600" },
  input: { minHeight: 52, borderWidth: 2, borderRadius: 10, paddingHorizontal: 14, fontSize: 20 },
  button: { minHeight: 52, borderRadius: 999, borderWidth: 2, alignItems: "center", justifyContent: "center", marginTop: 8 },
  buttonText: { fontSize: 19, fontWeight: "700" },
  card: { borderWidth: 1, borderRadius: 18, padding: 14 },
  row: { flexDirection: "row", alignItems: "center", minHeight: 48 },
  flex: { flex: 1 },
  group: { gap: 8 },
  choice: { borderWidth: 2, borderRadius: 10, paddingHorizontal: 12, minHeight: 52 },
  radio: { fontSize: 22, width: 32 },
  stepBtn: { width: 56, height: 48, borderWidth: 2, borderRadius: 10, alignItems: "center", justifyContent: "center" },
  stepText: { fontSize: 28, fontWeight: "700" },
  value: { flex: 1, textAlign: "center", fontSize: 20, fontWeight: "700" },
  fact: { paddingVertical: 6 },
  factKey: { fontSize: 15 },
  factVal: { fontSize: 18, fontWeight: "600" },
  item: { fontSize: 18, lineHeight: 26, paddingVertical: 10, borderBottomWidth: StyleSheet.hairlineWidth },
});
