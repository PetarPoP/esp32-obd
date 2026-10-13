import { StyleSheet, Text, View } from "react-native";

import { theme } from "../theme/theme";

type MetricCardProps = {
  label: string;
  value: string;
  emphasis?: boolean;
};

export function MetricCard({ label, value, emphasis = false }: MetricCardProps) {
  return (
    <View style={[styles.card, emphasis ? styles.cardAccent : null]}>
      <Text style={styles.label}>{label}</Text>
      <Text style={[styles.value, emphasis ? styles.valueAccent : null]}>{value}</Text>
    </View>
  );
}

const styles = StyleSheet.create({
  card: {
    flex: 1,
    minWidth: "47%",
    backgroundColor: theme.colors.surface,
    borderColor: theme.colors.border,
    borderWidth: 1,
    borderRadius: theme.radius.lg,
    padding: theme.spacing.md,
    gap: theme.spacing.xs,
  },
  cardAccent: {
    borderColor: theme.colors.accent,
  },
  label: {
    color: theme.colors.textSecondary,
    fontSize: 13,
  },
  value: {
    color: theme.colors.textPrimary,
    fontWeight: "700",
    fontSize: 26,
  },
  valueAccent: {
    color: theme.colors.accent,
  },
});
