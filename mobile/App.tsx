// Root: pair first, then five tabs. A plain tab bar (no navigation library) keeps the native
// build unchanged; it is a VoiceOver tab bar with the selected tab and unread alerts announced.
import { StatusBar } from "expo-status-bar";
import { useState } from "react";
import { ActivityIndicator, Pressable, StyleSheet, Text, View } from "react-native";
import { BrimProvider, useBrim } from "./src/BrimContext";
import { ActivityScreen } from "./src/screens/Activity";
import { AlertsScreen } from "./src/screens/Alerts";
import { HealthScreen } from "./src/screens/Health";
import { NowScreen } from "./src/screens/Now";
import { PairScreen } from "./src/screens/Pair";
import { SettingsScreen } from "./src/screens/Settings";
import { useColors } from "./src/ui";

const TABS = [
  { key: "now", label: "Now", Component: NowScreen },
  { key: "activity", label: "Activity", Component: ActivityScreen },
  { key: "alerts", label: "Alerts", Component: AlertsScreen },
  { key: "settings", label: "Settings", Component: SettingsScreen },
  { key: "health", label: "Health", Component: HealthScreen },
] as const;
type TabKey = (typeof TABS)[number]["key"];

function Root() {
  const c = useColors();
  const { connection, unreadAlerts } = useBrim();
  const [tab, setTab] = useState<TabKey>("now");

  if (connection === "loading")
    return (
      <View style={[styles.fill, styles.center, { backgroundColor: c.bg }]}>
        <ActivityIndicator accessibilityLabel="Loading" />
      </View>
    );
  if (connection === "no-hat" || connection === "unpaired") return <PairScreen />;

  const Active = TABS.find((t) => t.key === tab)!.Component;
  return (
    <View style={[styles.fill, { backgroundColor: c.bg }]}>
      <View style={styles.fill}>
        {/* key: remount on tab change so the new screen's title gets VoiceOver focus */}
        <Active key={tab} />
      </View>
      <View style={[styles.bar, { backgroundColor: c.card, borderTopColor: c.border }]}>
        <View accessibilityRole="tablist" style={styles.tabs}>
          {TABS.map((t) => {
            const selected = t.key === tab;
            const badge = t.key === "alerts" ? unreadAlerts : 0;
            return (
              <Pressable
                key={t.key}
                accessibilityRole="tab"
                accessibilityLabel={badge ? `${t.label}, ${badge} new` : t.label}
                accessibilityState={{ selected }}
                onPress={() => setTab(t.key)}
                style={styles.tab}
              >
                <Text style={[styles.tabText, { color: selected ? c.accent : c.muted }, selected && styles.tabSelected]}>{t.label}</Text>
                {badge > 0 ? (
                  <View style={[styles.badge, { backgroundColor: c.danger }]}>
                    <Text style={[styles.badgeText, { color: c.bg }]}>{badge > 99 ? "99+" : badge}</Text>
                  </View>
                ) : null}
                {selected ? <View style={[styles.underline, { backgroundColor: c.accent }]} /> : null}
              </Pressable>
            );
          })}
        </View>
      </View>
    </View>
  );
}

export default function App() {
  return (
    <BrimProvider>
      <StatusBar style="auto" />
      <Root />
    </BrimProvider>
  );
}

const styles = StyleSheet.create({
  fill: { flex: 1 },
  center: { justifyContent: "center", alignItems: "center" },
  // paddingBottom clears the iPhone home indicator (no safe-area package: keeps the native build unchanged).
  bar: { borderTopWidth: StyleSheet.hairlineWidth, paddingBottom: 26 },
  tabs: { flexDirection: "row" },
  tab: { flex: 1, minHeight: 56, alignItems: "center", justifyContent: "center", paddingVertical: 6 },
  tabText: { fontSize: 15, fontWeight: "600" },
  tabSelected: { fontWeight: "800" },
  underline: { position: "absolute", bottom: 4, height: 3, width: 28, borderRadius: 2 },
  badge: { position: "absolute", top: 4, right: 8, minWidth: 20, height: 20, borderRadius: 10, alignItems: "center", justifyContent: "center", paddingHorizontal: 4 },
  badgeText: { fontSize: 12, fontWeight: "800" },
});
