import { StatusBar } from "expo-status-bar";
import * as SystemUI from "expo-system-ui";
import { useEffect } from "react";
import { useColorScheme, View } from "react-native";
import { SafeAreaProvider, SafeAreaView } from "react-native-safe-area-context";

import { HomeScreen } from "./src/screens/HomeScreen";
import { useSettings } from "./src/state/settings";
import { darkScheme, lightScheme, ThemeContext } from "./src/theme/theme";

export default function App() {
  const store = useSettings();
  const system = useColorScheme();
  const pref = store.settings.theme;
  const colors = (pref === "system" ? system === "light" : pref === "light") ? lightScheme : darkScheme;

  useEffect(() => {
    SystemUI.setBackgroundColorAsync(colors.surface).catch(() => undefined);
  }, [colors]);

  return (
    <ThemeContext.Provider value={colors}>
      <SafeAreaProvider>
        <SafeAreaView style={{ flex: 1, backgroundColor: colors.surface }}>
          <StatusBar style={colors.dark ? "light" : "dark"} />
          {/* Wait for stored settings so the first link setup uses the saved car. */}
          {store.loaded ? <HomeScreen store={store} /> : <View style={{ flex: 1 }} />}
        </SafeAreaView>
      </SafeAreaProvider>
    </ThemeContext.Provider>
  );
}
