// First run (Figma O1–O4): hello, connect, hear Brim, start walking. "Connect" is the old Pair
// screen's logic (address + 6-digit PIN, same checks and messages), plus the laptop camera option.
import { useEffect, useState } from "react";
import { AccessibilityInfo, KeyboardAvoidingView, Pressable, ScrollView, StyleSheet, Text, TextInput, View } from "react-native";
import { ApiError, normalizeHatUrl } from "../api";
import { useBrim } from "../BrimContext";
import { normalizeCameraUrl } from "../camera";
import { Btn, Bunny, GlowPage, Seg, VoiceBar, Wave } from "../design";
import { T, tight, type BunnyName } from "../theme";
import { useFocusTitle } from "../ui";
import { say } from "../voice";

const TEST_PHRASE = "Chair ahead, step right.";

function Dots({ step }: { step: number }) {
  return (
    <View accessible accessibilityLabel={`Step ${step + 1} of 4`} style={o.dots}>
      {[0, 1, 2, 3].map((k) => (
        <View key={k} style={[o.dot, k === step && o.dotOn]} />
      ))}
    </View>
  );
}

function Step({ step, bunny, title, body, children, actions, voiceLabel }: {
  step: number;
  bunny: BunnyName;
  title: string;
  body: string;
  children?: React.ReactNode;
  actions: React.ReactNode;
  voiceLabel: string;
}) {
  const ref = useFocusTitle();
  return (
    <GlowPage tone="calm">
      <KeyboardAvoidingView behavior="padding" style={{ flex: 1 }}>
        <ScrollView contentContainerStyle={o.wrap} keyboardShouldPersistTaps="handled">
          <Dots step={step} />
          <Bunny name={bunny} size={210} style={o.bunny} />
          <Text ref={ref} accessibilityRole="header" style={[o.title, tight(34)]}>
            {title}
          </Text>
          <Text style={o.body}>{body}</Text>
          {children}
          <View style={{ flex: 1, minHeight: 24 }} />
          <View style={o.actions}>{actions}</View>
          <VoiceBar label={voiceLabel} hint="Brim reads this step out loud." onPress={() => say(`${title} ${body}`, "polite")} />
        </ScrollView>
      </KeyboardAvoidingView>
    </GlowPage>
  );
}

export function OnboardingScreen({ start, onDone }: { start: number; onDone: () => void }) {
  const { hatUrl, pair, connectCamera, connection } = useBrim();
  const [step, setStep] = useState(start);
  const [mode, setMode] = useState<"hat" | "camera">("hat");
  const [address, setAddress] = useState(hatUrl?.replace(/^https?:\/\//, "") ?? "");
  const [pin, setPin] = useState("");
  const [busy, setBusy] = useState(false);
  const [error, setError] = useState<string | null>(null);

  useEffect(() => setError(null), [mode, step]);

  const fail = (m: string) => {
    setError(m);
    AccessibilityInfo.announceForAccessibility(m);
  };

  // Same checks and messages as the old Pair screen.
  const connect = async () => {
    if (!address.trim()) return fail(mode === "hat" ? "The hat address box is empty. Type the address shown with the PIN, for example 192.168.1.42." : "Type the laptop's address from the camera page, for example 192.168.1.20.");
    if (mode === "camera") {
      const url = normalizeCameraUrl(address);
      if (!url) return fail("That address doesn't look right. Type the numbers from the camera page, for example 192.168.1.20.");
      await connectCamera(url);
      say("Connected to the laptop camera.", "polite");
      return setStep(2);
    }
    const url = normalizeHatUrl(address);
    if (!url) return fail("That address doesn't look right. Type just the numbers, for example 192.168.1.42 or 192.168.1.42:8080.");
    if (!/^\d{6}$/.test(pin)) return fail("The PIN is 6 digits.");
    setBusy(true);
    setError(null);
    try {
      await pair(url, pin);
      say("Paired with your hat.", "polite");
      setStep(2);
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

  if (step === 0)
    return (
      <Step
        step={0}
        bunny="brim"
        title="Hi, I'm Brim."
        body="I sit on your hat and tell you what is in front of you, so you can walk with confidence."
        voiceLabel="Tap to hear this step"
        actions={<Btn label="Get started" onPress={() => setStep(1)} />}
      />
    );

  if (step === 1)
    return (
      <Step
        step={1}
        bunny="wave"
        title={mode === "hat" ? "Turn on your hat" : "Use the laptop camera"}
        body={
          mode === "hat"
            ? connection === "unpaired"
              ? "Your hat no longer knows this phone. Enter its address and a new 6-digit PIN."
              : "Join the hat's Wi-Fi, then enter its address and the 6-digit PIN it gives you."
            : "On the laptop run the camera page, then type the address it shows under “Link a phone”."
        }
        voiceLabel="Tap to hear this step"
        actions={
          <>
            <Btn label={busy ? "Connecting…" : "Connect"} onPress={() => void connect()} disabled={busy} />
          </>
        }
      >
        <View style={o.form}>
          <Seg label="Connect to" value={mode} onChange={setMode} options={[["hat", "My hat"], ["camera", "Laptop camera"]]} />
          <Text nativeID="addr" style={o.label}>
            {mode === "hat" ? "Hat address" : "Laptop address"}
          </Text>
          <TextInput
            accessibilityLabel={mode === "hat" ? "Hat address" : "Laptop address"}
            accessibilityLabelledBy="addr"
            accessibilityHint={mode === "hat" ? "For example 192.168.1.42" : "For example 192.168.1.20"}
            value={address}
            onChangeText={setAddress}
            autoCapitalize="none"
            autoCorrect={false}
            keyboardType="url"
            placeholder={mode === "hat" ? "e.g. 192.168.1.42" : "e.g. 192.168.1.20"}
            placeholderTextColor={T.muted}
            style={o.input}
          />
          {mode === "hat" ? (
            <>
              <Text nativeID="pin" style={o.label}>
                PIN
              </Text>
              <TextInput
                accessibilityLabel="PIN, 6 digits"
                accessibilityLabelledBy="pin"
                value={pin}
                onChangeText={(t) => setPin(t.replace(/\D/g, "").slice(0, 6))}
                keyboardType="number-pad"
                textContentType="oneTimeCode"
                maxLength={6}
                onSubmitEditing={() => void connect()}
                style={[o.input, o.pin]}
              />
            </>
          ) : null}
          {error ? (
            <Text accessibilityLiveRegion="assertive" style={o.error}>
              {error}
            </Text>
          ) : null}
        </View>
      </Step>
    );

  if (step === 2)
    return (
      <Step
        step={2}
        bunny="curious"
        title="Can you hear me?"
        body="Put in your earpiece, then play a test."
        voiceLabel="Tap to hear this step"
        actions={
          <>
            <Btn label="I heard it" onPress={() => setStep(3)} />
            <Btn glass label="Play the test again" onPress={() => say(TEST_PHRASE, "polite")} />
          </>
        }
      >
        <Pressable accessibilityRole="button" accessibilityLabel={`Play the test: ${TEST_PHRASE}`} onPress={() => say(TEST_PHRASE, "polite")} style={o.test}>
          <Wave small />
          <Text style={[o.testText, tight(17)]}>“{TEST_PHRASE}”</Text>
        </Pressable>
      </Step>
    );

  return (
    <Step
      step={3}
      bunny="hop"
      title="You're all set"
      body="Walk forward slowly. I will tell you when something is in the way, and which way is clear."
      voiceLabel="Tap to hear this step"
      actions={<Btn label="Start walking" onPress={onDone} />}
    />
  );
}

const o = StyleSheet.create({
  wrap: { flexGrow: 1, paddingHorizontal: 22, paddingTop: 62, paddingBottom: 34, alignItems: "stretch" },
  dots: { flexDirection: "row", justifyContent: "center", gap: 6 },
  dot: { width: 8, height: 8, borderRadius: 4, backgroundColor: "#D9D3E8" },
  dotOn: { width: 22, backgroundColor: T.button },
  bunny: { alignSelf: "center", marginTop: 30, marginBottom: 4 },
  title: { color: T.ink, fontWeight: "700", textAlign: "center", lineHeight: 40, marginTop: 6 },
  body: { color: T.muted, fontSize: 17, lineHeight: 25, textAlign: "center", marginTop: 10, marginHorizontal: 6 },
  actions: { gap: 10, marginBottom: 12 },
  form: { marginTop: 22, gap: 8 },
  label: { color: T.ink, fontSize: 15, fontWeight: "600", marginTop: 6 },
  input: { minHeight: 52, borderRadius: 16, paddingHorizontal: 16, fontSize: 19, color: T.ink, backgroundColor: "rgba(255,255,255,0.85)" },
  pin: { letterSpacing: 10, fontSize: 24, fontWeight: "700", textAlign: "center" },
  error: { color: T.red, fontSize: 15, fontWeight: "700", marginTop: 4 },
  test: { flexDirection: "row", alignItems: "center", gap: 12, marginTop: 22, paddingVertical: 16, paddingHorizontal: 18, borderRadius: 999, backgroundColor: T.glass },
  testText: { color: T.ink, fontWeight: "600", flex: 1 },
});
