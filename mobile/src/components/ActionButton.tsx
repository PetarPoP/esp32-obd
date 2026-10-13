import { Pressable, StyleSheet, Text } from "react-native";

import { theme } from "../theme/theme";

type ActionButtonProps = {
  label: string;
  onPress: () => void;
};

export function ActionButton({ label, onPress }: ActionButtonProps) {
  return (
    <Pressable
      style={({ pressed }) => [styles.button, pressed ? styles.buttonPressed : null]}
      onPress={onPress}
    >
      <Text style={styles.text}>{label}</Text>
    </Pressable>
  );
}

const styles = StyleSheet.create({
  button: {
    backgroundColor: theme.colors.surface,
    borderColor: theme.colors.border,
    borderRadius: theme.radius.md,
    borderWidth: 1,
    paddingVertical: 12,
    paddingHorizontal: 14,
  },
  buttonPressed: {
    backgroundColor: "#1A2650",
    transform: [{ scale: 0.985 }],
    opacity: 0.9,
  },
  text: {
    color: theme.colors.textPrimary,
    fontWeight: "600",
    textAlign: "center",
  },
});
