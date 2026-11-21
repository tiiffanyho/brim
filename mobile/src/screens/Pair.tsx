import { useState } from "react";
import { AccessibilityInfo, ActivityIndicator, Text, TextInput } from "react-native";
import { ApiError, normalizeHatUrl } from "../api";
import { useBrim } from "../BrimContext";
import { Button, ErrorText, Screen, styles, useColors } from "../ui";

export function PairScreen() {
  const c = useColors();
  const { hatUrl, pair, connection } = useBrim();
  const [address, setAddress] = useState(hatUrl?.replace(/^https?:\/\//, "") ?? "");
  const [pin, setPin] = useState("");
  const [busy, setBusy] = useState(false);
  const [error, setError] = useState<string | null>(null);

  const fail = (m: string) => {
    setError(m);
    AccessibilityInfo.announceForAccessibility(m);
  };

  const submit = async () => {
    const url = normalizeHatUrl(address);
    if (!address.trim()) return fail("The hat address box is empty. Type the address shown with the PIN, for example 192.168.1.42.");
    if (!url) return fail("That address doesn't look right. Type just the numbers, for example 192.168.1.42 or 192.168.1.42:8080.");
    if (!/^\d{6}$/.test(pin)) return fail("The PIN is 6 digits.");
    setBusy(true);
    setError(null);
    try {
      await pair(url, pin);
      AccessibilityInfo.announceForAccessibility("Paired with your hat.");
    } catch (e) {
      fail(
        e instanceof ApiError
          ? e.status === 429
            ? `Too many tries. Wait ${e.retryAfterS ?? 60} seconds.`
            : e.unauthorized
              ? "That PIN is not right. Check it and try again."
              : e.message
          : "Pairing failed. Try again.",
      );
    } finally {
      setBusy(false);
    }
  };

  return (
    <Screen
      title="Pair with your hat"
      intro={
        connection === "unpaired"
          ? "Your hat no longer knows this phone. Enter a new PIN to pair again."
          : "Join the same Wi-Fi as your hat, then enter its address and the 6-digit PIN."
      }
    >
      <Text nativeID="addrLabel" style={[styles.label, { color: c.fg }]}>
        Hat address
      </Text>
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

      <Text nativeID="pinLabel" style={[styles.label, { color: c.fg }]}>
        PIN
      </Text>
      <TextInput
        accessibilityLabel="PIN, 6 digits"
        accessibilityLabelledBy="pinLabel"
        value={pin}
        onChangeText={(t) => setPin(t.replace(/\D/g, "").slice(0, 6))}
        keyboardType="number-pad"
        textContentType="oneTimeCode"
        maxLength={6}
        onSubmitEditing={() => void submit()}
        style={[styles.input, { letterSpacing: 6, color: c.fg, borderColor: c.border, backgroundColor: c.card }]}
      />

      <ErrorText error={error} />
      <Button label={busy ? "Pairing…" : "Pair"} onPress={() => void submit()} disabled={busy} />
      {busy ? <ActivityIndicator accessibilityLabel="Pairing" /> : null}
    </Screen>
  );
}
