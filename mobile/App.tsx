import { StatusBar } from "expo-status-bar";
import { ActivityIndicator, View } from "react-native";
import { BrimProvider, useBrim } from "./src/BrimContext";
import { LiveScreen, PairScreen } from "./src/screens";

function Root() {
  const { connection } = useBrim();
  if (connection === "loading")
    return (
      <View style={{ flex: 1, justifyContent: "center" }}>
        <ActivityIndicator accessibilityLabel="Loading" />
      </View>
    );
  if (connection === "no-hat" || connection === "unpaired") return <PairScreen />;
  return <LiveScreen />;
}

export default function App() {
  return (
    <BrimProvider>
      <StatusBar style="auto" />
      <Root />
    </BrimProvider>
  );
}
