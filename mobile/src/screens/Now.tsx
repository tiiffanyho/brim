// The wearer's main screen: is the hat connected, is the phone speaking, what was just said.
import { Text, View } from "react-native";
import { useBrim, type Connection } from "../BrimContext";
import { Body, H2, Screen, SwitchRow, styles, useColors } from "../ui";

function statusWords(connection: Connection, linkLost: boolean): string {
  if (connection === "online" && !linkLost) return "Connected to your hat";
  if (linkLost) return "Connection lost. Trying again";
  if (connection === "offline") return "Can't reach your hat. Trying again";
  return "Checking";
}

const TIER_WORDS = { alert: "Warning", nav: "Direction", status: "Status", info: "Seen" } as const;

export function NowScreen() {
  const c = useColors();
  const { connection, linkLost, speaking, setSpeaking, recent } = useBrim();
  const status = statusWords(connection, linkLost);
  const ok = connection === "online" && !linkLost;

  return (
    <Screen title="Brim">
      <View
        accessible
        accessibilityLabel={`Status: ${status}`}
        accessibilityLiveRegion="polite"
        style={[styles.card, { backgroundColor: c.card, borderColor: ok ? c.ok : c.danger }]}
      >
        <Text style={[styles.body, styles.bold, { color: ok ? c.fg : c.danger }]}>
          {ok ? "● " : "▲ "}
          {status}
        </Text>
      </View>

      <SwitchRow
        label="Speak warnings out loud"
        hint="Keeps speaking with the phone locked. Turn off to stay silent."
        value={speaking}
        onChange={setSpeaking}
      />
      {!speaking ? (
        <Text style={[styles.body, { color: c.danger }]}>Speech is off. You will not hear warnings while the phone is locked.</Text>
      ) : null}

      <H2>Recently said</H2>
      {recent.length === 0 ? (
        <Body muted>Nothing yet.</Body>
      ) : (
        recent.map((r) => (
          <Text key={r.id} style={[styles.item, { color: r.tier === "alert" ? c.danger : c.fg, borderColor: c.border }]}>
            <Text style={styles.bold}>{TIER_WORDS[r.tier]}: </Text>
            {r.text}
          </Text>
        ))
      )}
    </Screen>
  );
}
