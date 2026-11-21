// Two screens: pair with a hat, then the live screen. Built for VoiceOver first: every control has
// a label and hint, headers are marked, touch targets are at least 44 pt, and status is said in
// words (never colour alone).
import { useEffect, useRef, useState, type ComponentRef } from "react";
import {
  AccessibilityInfo,
  ActivityIndicator,
  Pressable,
  ScrollView,
  StyleSheet,
  Switch,
  Text,
  TextInput,
  View,
  useColorScheme,
} from "react-native";
import { ApiError, normalizeHatUrl } from "./api";
import { useBrim, type Connection } from "./BrimContext";

function useColors() {
  const dark = useColorScheme() === "dark";
  return dark
    ? { bg: "#0f1115", fg: "#f3f4f6", muted: "#b6bcc6", card: "#1b1f27", accent: "#7cc4ff", danger: "#ff8a80", border: "#3a4150" }
    : { bg: "#ffffff", fg: "#111418", muted: "#4a515c", card: "#f2f4f7", accent: "#0b5cad", danger: "#b3261e", border: "#c5cbd3" };
}

/** Moves VoiceOver focus to the screen title when a screen appears. */
function useFocusTitle() {
  const ref = useRef<ComponentRef<typeof Text>>(null);
  useEffect(() => {
    if (ref.current) AccessibilityInfo.sendAccessibilityEvent(ref.current, "focus");
  }, []);
  return ref;
}

function Button({ label, hint, onPress, disabled, kind = "primary" }: {
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

export function PairScreen() {
  const c = useColors();
  const { hatUrl, pair, connection } = useBrim();
  const title = useFocusTitle();
  const [address, setAddress] = useState(hatUrl?.replace(/^https?:\/\//, "") ?? "");
  const [pin, setPin] = useState("");
  const [busy, setBusy] = useState(false);
  const [error, setError] = useState<string | null>(null);

  const submit = async () => {
    const url = normalizeHatUrl(address);
    let msg: string | null = null;
    if (!address.trim()) msg = "The hat address box is empty. Type the address shown with the PIN, for example 192.168.1.42.";
    else if (!url) msg = "That address doesn't look right. Type just the numbers, for example 192.168.1.42 or 192.168.1.42:8080.";
    else if (!/^\d{6}$/.test(pin)) msg = "The PIN is 6 digits.";
    if (msg || !url) {
      setError(msg);
      AccessibilityInfo.announceForAccessibility(msg ?? "");
      return;
    }
    setBusy(true);
    setError(null);
    try {
      await pair(url, pin);
      AccessibilityInfo.announceForAccessibility("Paired with your hat.");
    } catch (e) {
      const m =
        e instanceof ApiError
          ? e.status === 429
            ? `Too many tries. Wait ${e.retryAfterS ?? 60} seconds.`
            : e.unauthorized
              ? "That PIN is not right. Check it and try again."
              : e.message
          : "Pairing failed. Try again.";
      setError(m);
      AccessibilityInfo.announceForAccessibility(m);
    } finally {
      setBusy(false);
    }
  };

  return (
    <ScrollView style={{ backgroundColor: c.bg }} contentContainerStyle={styles.screen} keyboardShouldPersistTaps="handled">
      <Text ref={title} accessibilityRole="header" style={[styles.h1, { color: c.fg }]}>
        Pair with your hat
      </Text>
      <Text style={[styles.body, { color: c.muted }]}>
        {connection === "unpaired"
          ? "Your hat no longer knows this phone. Enter a new PIN to pair again."
          : "Join the same Wi-Fi as your hat, then enter its address and the 6-digit PIN."}
      </Text>

      <Text nativeID="addrLabel" style={[styles.label, { color: c.fg }]}>Hat address</Text>
      <TextInput
        accessibilityLabel="Hat address"
        accessibilityLabelledBy="addrLabel"
        accessibilityHint="For example 192.168.1.42"
        value={address}
        onChangeText={setAddress}
        autoCapitalize="none"
        autoCorrect={false}
        keyboardType="url"
        placeholder="e.g. 192.168.1.42"
        placeholderTextColor={c.muted}
        style={[styles.input, { color: c.fg, borderColor: c.border, backgroundColor: c.card }]}
      />

      <Text nativeID="pinLabel" style={[styles.label, { color: c.fg }]}>PIN</Text>
      <TextInput
        accessibilityLabel="PIN, 6 digits"
        accessibilityLabelledBy="pinLabel"
        value={pin}
        onChangeText={(t) => setPin(t.replace(/\D/g, "").slice(0, 6))}
        keyboardType="number-pad"
        textContentType="oneTimeCode"
        maxLength={6}
        secureTextEntry={false}
        onSubmitEditing={() => void submit()}
        style={[styles.input, styles.pin, { color: c.fg, borderColor: c.border, backgroundColor: c.card }]}
      />

      {error ? (
        <Text accessibilityLiveRegion="assertive" style={[styles.error, { color: c.danger }]}>
          {error}
        </Text>
      ) : null}

      <Button label={busy ? "Pairing…" : "Pair"} onPress={() => void submit()} disabled={busy} />
      {busy ? <ActivityIndicator accessibilityLabel="Pairing" /> : null}
    </ScrollView>
  );
}

function statusWords(connection: Connection, linkLost: boolean): string {
  if (connection === "online" && !linkLost) return "Connected to your hat";
  if (linkLost) return "Connection lost. Trying again";
  if (connection === "offline") return "Can't reach your hat. Trying again";
  return "Checking";
}

const TIER_WORDS = { alert: "Warning", nav: "Direction", status: "Status", info: "Seen" } as const;

export function LiveScreen() {
  const c = useColors();
  const { hatUrl, connection, linkLost, speaking, setSpeaking, recent, health, liveHealth, forgetHat } = useBrim();
  const title = useFocusTitle();
  const status = statusWords(connection, linkLost);
  const ok = connection === "online" && !linkLost;

  return (
    <ScrollView style={{ backgroundColor: c.bg }} contentContainerStyle={styles.screen}>
      <Text ref={title} accessibilityRole="header" style={[styles.h1, { color: c.fg }]}>
        Brim
      </Text>

      <View
        accessible
        accessibilityLabel={`Status: ${status}`}
        accessibilityLiveRegion="polite"
        style={[styles.card, { backgroundColor: c.card, borderColor: ok ? c.accent : c.danger }]}
      >
        <Text style={[styles.status, { color: ok ? c.fg : c.danger }]}>
          {ok ? "● " : "▲ "}
          {status}
        </Text>
      </View>

      <View style={[styles.row, styles.card, { backgroundColor: c.card, borderColor: c.border }]}>
        <Text nativeID="speakLabel" style={[styles.body, styles.flex, { color: c.fg }]}>
          Speak warnings out loud
        </Text>
        <Switch
          accessibilityLabel="Speak warnings out loud"
          accessibilityHint="Keeps speaking with the phone locked. Turn off to stay silent."
          value={speaking}
          onValueChange={setSpeaking}
        />
      </View>
      {!speaking ? (
        <Text style={[styles.body, { color: c.danger }]}>
          Speech is off. You will not hear warnings while the phone is locked.
        </Text>
      ) : null}

      <Text accessibilityRole="header" style={[styles.h2, { color: c.fg }]}>
        Recently said
      </Text>
      {recent.length === 0 ? (
        <Text style={[styles.body, { color: c.muted }]}>Nothing yet.</Text>
      ) : (
        recent.map((r) => (
          <Text key={r.id} style={[styles.item, { color: r.tier === "alert" ? c.danger : c.fg, borderColor: c.border }]}>
            <Text style={styles.bold}>{TIER_WORDS[r.tier]}: </Text>
            {r.text}
          </Text>
        ))
      )}

      <Text accessibilityRole="header" style={[styles.h2, { color: c.fg }]}>
        Hat health
      </Text>
      <Text style={[styles.body, { color: c.fg }]}>
        {liveHealth
          ? `Camera ${Math.round(liveHealth.fps)} frames a second. Delay ${Math.round(liveHealth.pipeline_latency_ms)} milliseconds.`
          : "No health report yet."}
        {health ? ` Restarts: ${health.restart_count}.` : ""}
      </Text>
      <Text style={[styles.small, { color: c.muted }]}>Hat: {hatUrl?.replace(/^https?:\/\//, "")}</Text>

      <Button
        kind="secondary"
        label="Forget this hat"
        hint="Stops listening to this hat. You will need its PIN to pair again."
        onPress={() => void forgetHat()}
      />
    </ScrollView>
  );
}

const styles = StyleSheet.create({
  screen: { padding: 20, paddingTop: 64, gap: 12 },
  h1: { fontSize: 32, fontWeight: "700" },
  h2: { fontSize: 22, fontWeight: "700", marginTop: 12 },
  body: { fontSize: 18, lineHeight: 26 },
  small: { fontSize: 15 },
  bold: { fontWeight: "700" },
  label: { fontSize: 18, fontWeight: "600", marginTop: 8 },
  input: { minHeight: 52, borderWidth: 2, borderRadius: 10, paddingHorizontal: 14, fontSize: 20 },
  pin: { letterSpacing: 6, fontVariant: ["tabular-nums"] },
  error: { fontSize: 18, fontWeight: "600" },
  button: { minHeight: 52, borderRadius: 12, borderWidth: 2, alignItems: "center", justifyContent: "center", marginTop: 8 },
  buttonText: { fontSize: 19, fontWeight: "700" },
  card: { borderWidth: 2, borderRadius: 12, padding: 14 },
  status: { fontSize: 20, fontWeight: "700" },
  row: { flexDirection: "row", alignItems: "center", minHeight: 56 },
  flex: { flex: 1 },
  item: { fontSize: 18, lineHeight: 26, paddingVertical: 8, borderBottomWidth: StyleSheet.hairlineWidth },
});
