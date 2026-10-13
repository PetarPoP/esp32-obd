import { memo } from "react";
import { ScrollView, StyleSheet, Text, View } from "react-native";

import { theme } from "../theme/theme";

type ConsoleViewProps = {
  lines: readonly string[];
};

// Display window — even if the hook keeps up to 300 lines in state, the console
// only ever paints the most recent kVisibleLines into the RN bridge. Capping
// the actual <Text> view count is what keeps long streams responsive on
// low-end Android phones (each <Text> is a real native view).
const kVisibleLines = 100;

function ConsoleViewImpl({ lines }: ConsoleViewProps) {
  if (lines.length === 0) {
    return (
      <View style={styles.box}>
        <Text style={styles.empty}>No BLE logs yet</Text>
      </View>
    );
  }
  const visible: string[] = [];
  const start = Math.max(0, lines.length - kVisibleLines);
  for (let i = lines.length - 1; i >= start; --i) {
    visible.push(lines[i]);
  }
  return (
    <View style={styles.box}>
      <ScrollView
        style={styles.scroll}
        contentContainerStyle={styles.scrollContent}
        nestedScrollEnabled
        showsVerticalScrollIndicator
      >
        {visible.map((line, idx) => (
          <Text key={`${start + idx}`} style={styles.line}>
            {line}
          </Text>
        ))}
      </ScrollView>
    </View>
  );
}

// Memoized so that unrelated dashboard state changes (telemetry, status,
// linkState updates several times per second) do not trigger a console
// re-render. The console re-paints only when `lines` reference changes — once
// per ≤ 250 ms thanks to the batched flush in useObdMonitor.
export const ConsoleView = memo(ConsoleViewImpl, (prev, next) => prev.lines === next.lines);

const styles = StyleSheet.create({
  box: {
    backgroundColor: theme.colors.surface,
    borderRadius: theme.radius.md,
    borderWidth: 1,
    borderColor: theme.colors.border,
    height: 240,
    overflow: "hidden",
  },
  scroll: {
    flex: 1,
    paddingHorizontal: theme.spacing.md,
  },
  scrollContent: {
    paddingVertical: theme.spacing.md,
    gap: 6,
  },
  line: {
    color: theme.colors.textSecondary,
    fontSize: 12,
    fontFamily: "monospace",
  },
  empty: {
    color: theme.colors.textSecondary,
    fontSize: 13,
    padding: theme.spacing.md,
  },
});
