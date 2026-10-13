import { Pressable, StyleSheet, Text, View } from "react-native";

import { theme } from "../theme/theme";

type Props = {
  label: string;
  sublabel?: string;
  checked: boolean;
  onToggle: () => void;
};

export function MetricCheckboxRow({ label, sublabel, checked, onToggle }: Props) {
  return (
    <Pressable
      onPress={onToggle}
      style={({ pressed }) => [styles.row, pressed ? styles.rowPressed : null]}
    >
      <View style={[styles.box, checked ? styles.boxChecked : null]}>
        {checked ? <Text style={styles.checkMark}>x</Text> : null}
      </View>
      <View style={styles.labels}>
        <Text style={styles.label}>{label}</Text>
        {sublabel ? <Text style={styles.sublabel}>{sublabel}</Text> : null}
      </View>
    </Pressable>
  );
}

const styles = StyleSheet.create({
  row: {
    flexDirection: "row",
    alignItems: "center",
    paddingVertical: 8,
    gap: theme.spacing.sm,
  },
  rowPressed: {
    opacity: 0.7,
  },
  box: {
    width: 22,
    height: 22,
    borderWidth: 1,
    borderColor: theme.colors.border,
    borderRadius: theme.radius.sm,
    backgroundColor: theme.colors.surface,
    alignItems: "center",
    justifyContent: "center",
  },
  boxChecked: {
    borderColor: theme.colors.accent,
    backgroundColor: theme.colors.accent,
  },
  checkMark: {
    color: theme.colors.background,
    fontWeight: "800",
    fontSize: 14,
  },
  labels: {
    flexShrink: 1,
  },
  label: {
    color: theme.colors.textPrimary,
    fontSize: 14,
    fontWeight: "600",
  },
  sublabel: {
    color: theme.colors.textSecondary,
    fontSize: 12,
  },
});
