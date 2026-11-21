// Building blocks for the redesigned screens. Same VoiceOver rules as ui.tsx: every control has a
// role and a label, switches and choices announce their state, the scale is "adjustable", and
// touch targets are at least 44 pt. Flat: no shadows anywhere.
import { useEffect, useRef, type ReactNode } from "react";
import { AccessibilityInfo, Animated, Image, ImageBackground, Pressable, StyleSheet, Text, View, type ViewStyle } from "react-native";
import { BUNNY, GLOW, T, TONE_COLOR, tight, type BunnyName, type Tone } from "./theme";

/** Full-screen soft glow behind a screen. */
export function GlowPage({ tone = "calm", children, style }: { tone?: Tone; children: ReactNode; style?: ViewStyle }) {
  return (
    <ImageBackground source={GLOW[tone]} resizeMode="cover" style={[s.page, style]} accessibilityIgnoresInvertColors>
      {children}
    </ImageBackground>
  );
}

export function Bunny({ name, size, style }: { name: BunnyName; size: number; style?: ViewStyle }) {
  return (
    <View style={style} accessibilityElementsHidden importantForAccessibility="no-hide-descendants">
      <Image source={BUNNY[name]} style={{ width: size, height: size }} resizeMode="contain" />
    </View>
  );
}

/** The little sound wave. Moves gently unless Reduce Motion is on. */
export function Wave({ tone = "calm", small }: { tone?: Tone; small?: boolean }) {
  const heights = small ? [8, 16, 24, 12, 20, 10] : [10, 20, 32, 16, 26, 36, 18, 28, 12, 22, 14];
  const v = useRef(new Animated.Value(1)).current;
  useEffect(() => {
    let loop: Animated.CompositeAnimation | null = null;
    void AccessibilityInfo.isReduceMotionEnabled().then((reduce) => {
      if (reduce) return;
      loop = Animated.loop(
        Animated.sequence([
          Animated.timing(v, { toValue: 0.55, duration: 600, useNativeDriver: true }),
          Animated.timing(v, { toValue: 1, duration: 600, useNativeDriver: true }),
        ]),
      );
      loop.start();
    });
    return () => loop?.stop();
  }, [v]);
  return (
    <View style={[s.wave, small && { height: 24, gap: 3 }]} accessibilityElementsHidden importantForAccessibility="no-hide-descendants">
      {heights.map((h, i) => (
        <Animated.View
          key={i}
          style={{
            width: small ? 3 : 4,
            height: h,
            borderRadius: 2,
            backgroundColor: small ? T.purple : TONE_COLOR[tone],
            transform: [{ scaleY: i % 2 ? v : Animated.subtract(1.55, v) }],
          }}
        />
      ))}
    </View>
  );
}

/** The light-glass bar at the bottom. Tapping it says the hint's action. */
export function VoiceBar({ label, hint, onPress }: { label: string; hint: string; onPress: () => void }) {
  return (
    <Pressable accessibilityRole="button" accessibilityLabel={label} accessibilityHint={hint} onPress={onPress} style={({ pressed }) => [s.voice, pressed && { backgroundColor: T.glassStrong }]}>
      <View style={s.mic}>
        <View style={s.micBody} />
        <View style={s.micCup} />
        <View style={s.micStand} />
      </View>
      <Text style={[s.voiceText, tight(15)]} numberOfLines={1}>
        {label}
      </Text>
      <View style={s.dots}>
        {[6, 12, 8, 14, 6].map((h, i) => (
          <View key={i} style={{ width: 3, height: h, borderRadius: 2, backgroundColor: "rgba(109,40,217,0.35)" }} />
        ))}
      </View>
    </Pressable>
  );
}

export function Btn({ label, hint, onPress, glass, disabled }: { label: string; hint?: string; onPress: () => void; glass?: boolean; disabled?: boolean }) {
  return (
    <Pressable
      accessibilityRole="button"
      accessibilityLabel={label}
      accessibilityHint={hint}
      accessibilityState={{ disabled: !!disabled }}
      disabled={disabled}
      onPress={onPress}
      style={({ pressed }) => [s.btn, glass ? s.btnGlass : s.btnSolid, { opacity: disabled ? 0.45 : pressed ? 0.85 : 1 }]}
    >
      <Text style={[s.btnText, tight(17), { color: glass ? T.button : "#fff" }]}>{label}</Text>
    </Pressable>
  );
}

/** Flat switch row. Purple for "Use Brim for", dark purple for fine-tuning; off is lavender. */
export function ToggleRow({ label, sub, value, onChange, dark, locked }: {
  label: string;
  sub?: string;
  value: boolean;
  onChange?: (v: boolean) => void;
  dark?: boolean;
  /** Always on: shown for honesty, can't be switched off. */
  locked?: boolean;
}) {
  const on = locked || value;
  return (
    <Pressable
      accessibilityRole="switch"
      accessibilityLabel={label}
      accessibilityHint={locked ? "Always on. Safety warnings can't be switched off." : sub}
      accessibilityState={{ checked: on, disabled: !!locked }}
      disabled={locked}
      onPress={() => onChange?.(!value)}
      style={s.row}
    >
      <View style={{ flex: 1, gap: 2 }}>
        <Text style={[s.rowTitle, tight(15)]}>{label}</Text>
        {sub ? <Text style={s.rowSub}>{sub}</Text> : null}
      </View>
      {locked ? (
        <Text style={s.always}>Always on</Text>
      ) : (
        <View style={[s.switch, { backgroundColor: on ? (dark ? T.dark : T.purple) : T.lavender }]}>
          <View style={[s.knob, { transform: [{ translateX: on ? 20 : 0 }] }]} />
        </View>
      )}
    </Pressable>
  );
}

/** One-of-many as a flat pill row (radio buttons for VoiceOver). */
export function Seg<V extends string | number>({ label, value, options, onChange }: {
  label: string;
  value: V;
  options: readonly (readonly [V, string])[];
  onChange: (v: V) => void;
}) {
  return (
    <View accessibilityRole="radiogroup" accessibilityLabel={label} style={s.seg}>
      {options.map(([v, l], i) => {
        const on = v === value;
        return (
          <Pressable
            key={String(v)}
            accessibilityRole="radio"
            accessibilityLabel={l}
            accessibilityState={{ selected: on, checked: on }}
            accessibilityHint={`${label}, ${i + 1} of ${options.length}`}
            onPress={() => onChange(v)}
            style={[s.segBtn, on && { backgroundColor: T.dark }]}
          >
            <Text style={[s.segText, on && { color: "#fff", fontWeight: "700" }]}>{l}</Text>
          </Pressable>
        );
      })}
    </View>
  );
}

/** Left-to-right scale with stops. One "adjustable" element for VoiceOver (swipe up / down). */
export function Scale({ label, stops, value, onChange }: { label: string; stops: readonly string[]; value: number; onChange: (v: number) => void }) {
  const set = (v: number) => onChange(Math.max(0, Math.min(stops.length - 1, v)));
  const pct = stops.length > 1 ? value / (stops.length - 1) : 0;
  return (
    <View
      accessible
      accessibilityRole="adjustable"
      accessibilityLabel={label}
      accessibilityValue={{ text: stops[value] }}
      accessibilityActions={[{ name: "increment" }, { name: "decrement" }]}
      onAccessibilityAction={(e) => set(value + (e.nativeEvent.actionName === "increment" ? 1 : -1))}
      style={{ gap: 12 }}
    >
      <View style={s.track}>
        <View style={s.rail} />
        <View style={[s.fill, { width: `${pct * 100}%` }]} />
        {stops.map((_, i) => (
          <Pressable
            key={i}
            onPress={() => set(i)}
            hitSlop={10}
            style={[s.stop, { left: `${(i / (stops.length - 1)) * 100}%` }]}
            importantForAccessibility="no"
            accessibilityElementsHidden
          >
            <View style={i === value ? s.thumb : s.stopDot} />
          </Pressable>
        ))}
      </View>
      <View style={s.scaleLabels}>
        {stops.map((l, i) => (
          <Pressable key={l} onPress={() => set(i)} hitSlop={8} importantForAccessibility="no" accessibilityElementsHidden>
            <Text style={[s.scaleLabel, i === value && { color: T.dark, fontWeight: "700" }]}>{l}</Text>
          </Pressable>
        ))}
      </View>
    </View>
  );
}

/** Label / value pairs on the page itself (no card), each read as one sentence. */
export function Rows({ rows }: { rows: readonly (readonly [string, string, ("red" | "green")?])[] }) {
  return (
    <View style={{ gap: 12 }}>
      {rows.map(([k, v, hl]) => (
        <View key={k} accessible accessibilityLabel={`${k}: ${v}`} style={{ flexDirection: "row", gap: 12 }}>
          <Text style={s.rowKey}>{k}</Text>
          <Text style={[s.rowVal, hl === "red" && { color: T.red, fontWeight: "700" }, hl === "green" && { color: T.green, fontWeight: "700" }]}>{v}</Text>
        </View>
      ))}
    </View>
  );
}

export function Kicker({ children, color = T.muted }: { children: string; color?: string }) {
  return <Text style={[s.kicker, { color }]}>{children.toUpperCase()}</Text>;
}

export const s = StyleSheet.create({
  page: { flex: 1, backgroundColor: T.bg },
  wave: { flexDirection: "row", alignItems: "center", gap: 4, height: 36 },
  voice: { flexDirection: "row", alignItems: "center", gap: 12, minHeight: 60, paddingVertical: 10, paddingLeft: 10, paddingRight: 20, borderRadius: 999, backgroundColor: T.glass },
  mic: { width: 40, height: 40, borderRadius: 20, backgroundColor: T.glassStrong, alignItems: "center", justifyContent: "center" },
  micBody: { width: 9, height: 13, borderRadius: 5, borderWidth: 2, borderColor: T.button },
  micCup: { width: 15, height: 7, marginTop: -4, borderBottomLeftRadius: 8, borderBottomRightRadius: 8, borderWidth: 2, borderTopWidth: 0, borderColor: T.button },
  micStand: { width: 2, height: 3, backgroundColor: T.button },
  voiceText: { flex: 1, color: "rgba(27,21,48,0.8)", fontWeight: "500" },
  dots: { flexDirection: "row", alignItems: "center", gap: 3 },
  btn: { minHeight: 56, borderRadius: 999, alignItems: "center", justifyContent: "center", paddingHorizontal: 20 },
  btnSolid: { backgroundColor: T.button },
  btnGlass: { backgroundColor: T.glass },
  btnText: { fontWeight: "600" },
  row: { flexDirection: "row", alignItems: "center", gap: 12, minHeight: 56, paddingVertical: 12, paddingHorizontal: 16 },
  rowTitle: { color: T.ink, fontWeight: "600" },
  rowSub: { color: T.muted, fontSize: 12, lineHeight: 16 },
  always: { color: T.purple, fontSize: 13, fontWeight: "700" },
  switch: { width: 48, height: 28, borderRadius: 14, padding: 3 },
  knob: { width: 22, height: 22, borderRadius: 11, backgroundColor: "#fff" },
  seg: { flexDirection: "row", gap: 4, padding: 4, borderRadius: 999, backgroundColor: "rgba(255,255,255,0.75)" },
  segBtn: { flex: 1, minHeight: 44, borderRadius: 999, alignItems: "center", justifyContent: "center" },
  segText: { color: T.muted, fontSize: 14, fontWeight: "600" },
  track: { height: 34, justifyContent: "center", marginHorizontal: 17 },
  rail: { position: "absolute", left: 0, right: 0, height: 12, borderRadius: 6, backgroundColor: T.lavender },
  fill: { position: "absolute", left: 0, height: 12, borderRadius: 6, backgroundColor: "#A855F7" },
  stop: { position: "absolute", width: 34, height: 34, marginLeft: -17, alignItems: "center", justifyContent: "center" },
  stopDot: { width: 8, height: 8, borderRadius: 4, backgroundColor: "#fff" },
  thumb: { width: 34, height: 34, borderRadius: 17, backgroundColor: T.dark, borderWidth: 4, borderColor: "#fff" },
  scaleLabels: { flexDirection: "row", justifyContent: "space-between" },
  scaleLabel: { color: T.muted, fontSize: 13, fontWeight: "600" },
  rowKey: { width: 100, color: "rgba(94,88,115,0.9)", fontSize: 14, fontWeight: "500" },
  rowVal: { flex: 1, color: T.ink, fontSize: 15, lineHeight: 21, fontWeight: "500" },
  kicker: { fontSize: 12, fontWeight: "700", letterSpacing: 0.5 },
});
