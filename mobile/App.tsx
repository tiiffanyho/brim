// Root. First run: onboarding (hello, connect a hat or the laptop camera, hear Brim, start). Then
// the Live screen, with Settings behind the button in the top right; Alerts, Activity and Hat
// health open from Settings. No navigation library: keeps the native build unchanged.
import AsyncStorage from "@react-native-async-storage/async-storage";
import { StatusBar } from "expo-status-bar";
import { useEffect, useState } from "react";
import { ActivityIndicator, StyleSheet, View } from "react-native";
import { BrimProvider, useBrim } from "./src/BrimContext";
import { ActivityScreen } from "./src/screens/Activity";
import { AlertsScreen } from "./src/screens/Alerts";
import { HealthScreen } from "./src/screens/Health";
import { LiveScreen } from "./src/screens/Live";
import { OnboardingScreen } from "./src/screens/Onboarding";
import { SettingsScreen, type SubPage } from "./src/screens/Settings";
import { T } from "./src/theme";
import { BackContext } from "./src/ui";

const ONBOARDED_KEY = "brim.onboarded";
type View_ = "live" | "settings" | SubPage;

function Root() {
  const { connection } = useBrim();
  const [onboarded, setOnboarded] = useState<boolean | null>(null);
  const [view, setView] = useState<View_>("live");

  useEffect(() => {
    void AsyncStorage.getItem(ONBOARDED_KEY).then((v) => setOnboarded(v === "1"));
  }, []);

  if (connection === "loading" || onboarded === null)
    return (
      <View style={[styles.fill, styles.center]}>
        <ActivityIndicator accessibilityLabel="Loading" color={T.button} />
      </View>
    );

  // No hat or camera yet (or the hat forgot this phone): onboarding. Returning users skip hello.
  const needsConnect = connection === "no-hat" || connection === "unpaired";
  if (!onboarded || needsConnect)
    return (
      <OnboardingScreen
        key="onboarding"
        start={onboarded || connection === "unpaired" ? 1 : 0}
        onDone={() => {
          void AsyncStorage.setItem(ONBOARDED_KEY, "1");
          setOnboarded(true);
          setView("live");
        }}
      />
    );

  if (view === "live") return <LiveScreen key="live" openSettings={() => setView("settings")} />;
  if (view === "settings") return <SettingsScreen key="settings" onDone={() => setView("live")} open={setView} />;

  const Sub = view === "alerts" ? AlertsScreen : view === "activity" ? ActivityScreen : HealthScreen;
  return (
    <BackContext.Provider value={{ label: "Settings", onBack: () => setView("settings") }}>
      <Sub key={view} />
    </BackContext.Provider>
  );
}

export default function App() {
  return (
    <BrimProvider>
      <StatusBar style="dark" />
      <View style={styles.fill}>
        <Root />
      </View>
    </BrimProvider>
  );
}

const styles = StyleSheet.create({
  fill: { flex: 1, backgroundColor: T.bg },
  center: { justifyContent: "center", alignItems: "center" },
});
