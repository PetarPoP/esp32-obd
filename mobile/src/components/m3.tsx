// Small Material 3 component set for the "3a" design: buttons, cards, sheets,
// dialog, switch, list rows and snackbar, all colored from the theme context.
import MaterialIcons from "@expo/vector-icons/MaterialIcons";
import { useEffect, useRef, type ComponentProps, type ReactNode } from "react";
import {
  Animated,
  KeyboardAvoidingView,
  Modal,
  Platform,
  Pressable,
  ScrollView,
  StyleSheet,
  Text,
  TextInput,
  View,
  type StyleProp,
  type TextStyle,
  type ViewStyle,
} from "react-native";
import { useSafeAreaInsets } from "react-native-safe-area-context";

import { type, useColors } from "../theme/theme";

export type IconName = ComponentProps<typeof MaterialIcons>["name"];

export function Icon({ name, size = 24, color }: { name: IconName; size?: number; color?: string }) {
  const c = useColors();
  return <MaterialIcons name={name} size={size} color={color ?? c.onSurfaceVariant} />;
}

export function T({
  children,
  variant = "body",
  color,
  style,
  numberOfLines,
}: {
  children: ReactNode;
  variant?: keyof typeof type;
  color?: string;
  style?: StyleProp<TextStyle>;
  numberOfLines?: number;
}) {
  const c = useColors();
  return (
    <Text numberOfLines={numberOfLines} style={[type[variant], { color: color ?? c.onSurface }, style]}>
      {children}
    </Text>
  );
}

type ButtonKind = "filled" | "tonal" | "outlined" | "text";

export function Button({
  label,
  onPress,
  kind = "filled",
  icon,
  disabled,
  style,
}: {
  label: string;
  onPress: () => void;
  kind?: ButtonKind;
  icon?: IconName;
  disabled?: boolean;
  style?: StyleProp<ViewStyle>;
}) {
  const c = useColors();
  const bg = { filled: c.primary, tonal: c.secondaryContainer, outlined: "transparent", text: "transparent" }[kind];
  const fg = { filled: c.onPrimary, tonal: c.onSecondaryContainer, outlined: c.primary, text: c.primary }[kind];
  return (
    <Pressable
      onPress={disabled ? undefined : onPress}
      style={({ pressed }) => [
        styles.button,
        {
          backgroundColor: bg,
          borderColor: kind === "outlined" ? c.outline : "transparent",
          paddingLeft: icon ? 16 : kind === "text" ? 12 : 24,
          paddingRight: kind === "text" ? 12 : 24,
          opacity: disabled ? 0.38 : pressed ? 0.85 : 1,
        },
        style,
      ]}
    >
      {icon ? <MaterialIcons name={icon} size={18} color={fg} /> : null}
      <Text style={[type.titleSmall, { color: fg }]}>{label}</Text>
    </Pressable>
  );
}

export function IconButton({ icon, onPress, color }: { icon: IconName; onPress: () => void; color?: string }) {
  const c = useColors();
  return (
    <Pressable
      onPress={onPress}
      hitSlop={4}
      style={({ pressed }) => [styles.iconButton, pressed && { backgroundColor: c.dark ? "#ffffff14" : "#0000000f" }]}
    >
      <MaterialIcons name={icon} size={24} color={color ?? c.onSurfaceVariant} />
    </Pressable>
  );
}

export function Card({
  children,
  level = "low",
  style,
  onPress,
}: {
  children: ReactNode;
  level?: "low" | "mid" | "high";
  style?: StyleProp<ViewStyle>;
  onPress?: () => void;
}) {
  const c = useColors();
  const bg = { low: c.scLow, mid: c.sc, high: c.scHigh }[level];
  const body = <View style={[styles.card, { backgroundColor: bg }, style]}>{children}</View>;
  return onPress ? (
    <Pressable onPress={onPress} style={({ pressed }) => ({ opacity: pressed ? 0.9 : 1 })}>
      {body}
    </Pressable>
  ) : (
    body
  );
}

/** M3 switch (52×32). */
export function Switch({ value, onChange }: { value: boolean; onChange: (v: boolean) => void }) {
  const c = useColors();
  const size = value ? 24 : 16;
  return (
    <Pressable
      onPress={() => onChange(!value)}
      hitSlop={8}
      style={[
        styles.switchTrack,
        { borderColor: value ? c.primary : c.outline, backgroundColor: value ? c.primary : c.scHighest },
      ]}
    >
      <View
        style={{
          position: "absolute",
          left: value ? 24 : 6,
          width: size,
          height: size,
          borderRadius: size / 2,
          backgroundColor: value ? c.onPrimary : c.outline,
        }}
      />
    </Pressable>
  );
}

export function Checkbox({ checked }: { checked: boolean }) {
  const c = useColors();
  return <MaterialIcons name={checked ? "check-box" : "check-box-outline-blank"} size={24} color={checked ? c.primary : c.onSurfaceVariant} />;
}

export function Radio({ selected }: { selected: boolean }) {
  const c = useColors();
  return (
    <MaterialIcons
      name={selected ? "radio-button-checked" : "radio-button-unchecked"}
      size={22}
      color={selected ? c.primary : c.onSurfaceVariant}
    />
  );
}

/** One list row: leading icon or control, title, optional supporting text, trailing element. */
export function ListRow({
  title,
  subtitle,
  leading,
  trailing,
  onPress,
  disabled,
}: {
  title: string;
  subtitle?: string;
  leading?: ReactNode;
  trailing?: ReactNode;
  onPress?: () => void;
  disabled?: boolean;
}) {
  const c = useColors();
  return (
    <Pressable
      onPress={disabled ? undefined : onPress}
      style={({ pressed }) => [
        styles.listRow,
        { minHeight: subtitle ? 64 : 56, opacity: disabled ? 0.5 : 1 },
        pressed && onPress ? { backgroundColor: c.dark ? "#ffffff0d" : "#0000000a" } : null,
      ]}
    >
      {leading}
      <View style={{ flex: 1, minWidth: 0 }}>
        <T>{title}</T>
        {subtitle ? <T variant="bodySmall" color={c.onSurfaceVariant}>{subtitle}</T> : null}
      </View>
      {trailing}
    </Pressable>
  );
}

/** Bottom sheet: scrim, drag handle, title and close button, scrolling body. */
export function Sheet({
  visible,
  title,
  onClose,
  children,
  footer,
  scroll = true,
}: {
  visible: boolean;
  title: string;
  onClose: () => void;
  children: ReactNode;
  footer?: ReactNode;
  scroll?: boolean;
}) {
  const c = useColors();
  const insets = useSafeAreaInsets();
  // The scrim fades in with the modal while the sheet itself slides up.
  const slide = useRef(new Animated.Value(0)).current;
  useEffect(() => {
    if (!visible) return;
    slide.setValue(0);
    Animated.timing(slide, { toValue: 1, duration: 250, useNativeDriver: Platform.OS !== "web" }).start();
  }, [visible, slide]);
  const translateY = slide.interpolate({ inputRange: [0, 1], outputRange: [300, 0] });
  return (
    <Modal visible={visible} transparent animationType="fade" onRequestClose={onClose} statusBarTranslucent>
      <KeyboardAvoidingView behavior={Platform.OS === "ios" ? "padding" : undefined} style={{ flex: 1 }}>
        <Pressable style={[StyleSheet.absoluteFill, { backgroundColor: c.scrim }]} onPress={onClose} />
        <Animated.View style={[styles.sheet, { backgroundColor: c.scLow, paddingBottom: insets.bottom, transform: [{ translateY }] }]}>
          <View style={styles.handleRow}>
            <View style={[styles.handle, { backgroundColor: c.onSurfaceVariant }]} />
          </View>
          <View style={styles.sheetHeader}>
            <T variant="headline" style={{ flex: 1 }}>
              {title}
            </T>
            <IconButton icon="close" onPress={onClose} />
          </View>
          {scroll ? (
            <ScrollView contentContainerStyle={styles.sheetBody} keyboardShouldPersistTaps="handled">
              {children}
            </ScrollView>
          ) : (
            <View style={[styles.sheetBody, { flexShrink: 1 }]}>{children}</View>
          )}
          {footer ? <View style={styles.sheetFooter}>{footer}</View> : null}
        </Animated.View>
      </KeyboardAvoidingView>
    </Modal>
  );
}

/** M3 basic dialog. */
export function Dialog({
  visible,
  icon,
  title,
  text,
  onDismiss,
  actions,
  children,
}: {
  visible: boolean;
  icon?: IconName;
  title: string;
  text?: string;
  onDismiss: () => void;
  actions: { label: string; onPress: () => void }[];
  children?: ReactNode;
}) {
  const c = useColors();
  return (
    <Modal visible={visible} transparent animationType="fade" onRequestClose={onDismiss} statusBarTranslucent>
      <View style={[styles.dialogScrim, { backgroundColor: c.scrim }]}>
        <View style={[styles.dialog, { backgroundColor: c.scHigh }]}>
          {icon ? <Icon name={icon} /> : null}
          <T variant="headline" style={{ textAlign: "center" }}>
            {title}
          </T>
          {text ? (
            <T variant="bodySmall" color={c.onSurfaceVariant}>
              {text}
            </T>
          ) : null}
          {children}
          <View style={styles.dialogActions}>
            {actions.map((a) => (
              <Button key={a.label} kind="text" label={a.label} onPress={a.onPress} />
            ))}
          </View>
        </View>
      </View>
    </Modal>
  );
}

export function Field({
  value,
  onChangeText,
  placeholder,
  keyboardType,
  secure,
  maxLength,
}: {
  value: string;
  onChangeText: (v: string) => void;
  placeholder?: string;
  keyboardType?: "default" | "number-pad";
  secure?: boolean;
  maxLength?: number;
}) {
  const c = useColors();
  return (
    <TextInput
      value={value}
      onChangeText={onChangeText}
      placeholder={placeholder}
      placeholderTextColor={c.onSurfaceVariant}
      keyboardType={keyboardType}
      secureTextEntry={secure}
      maxLength={maxLength}
      style={[styles.field, type.body, { color: c.onSurface, borderColor: c.outline, backgroundColor: c.scHighest }]}
    />
  );
}

export function Snackbar({ text }: { text: string | null }) {
  const c = useColors();
  if (!text) return null;
  return (
    <View pointerEvents="none" style={[styles.snackbar, { backgroundColor: c.inverseSurface }]}>
      <T variant="bodySmall" color={c.inverseOnSurface}>
        {text}
      </T>
    </View>
  );
}

export function ProgressBar({ progress }: { progress: number }) {
  const c = useColors();
  return (
    <View style={[styles.progressTrack, { backgroundColor: c.secondaryContainer }]}>
      <View style={{ height: 4, borderRadius: 2, backgroundColor: c.primary, width: `${Math.round(Math.min(1, Math.max(0, progress)) * 100)}%` }} />
    </View>
  );
}

/** M3 segmented buttons (single choice). */
export function Segmented<V extends string>({
  options,
  value,
  onChange,
}: {
  options: { value: V; label: string }[];
  value: V;
  onChange: (v: V) => void;
}) {
  const c = useColors();
  return (
    <View style={[styles.segmented, { borderColor: c.outline }]}>
      {options.map((o, i) => {
        const selected = o.value === value;
        return (
          <Pressable
            key={o.value}
            onPress={() => onChange(o.value)}
            style={[
              styles.segment,
              { backgroundColor: selected ? c.secondaryContainer : "transparent" },
              i > 0 && { borderLeftWidth: 1, borderLeftColor: c.outline },
            ]}
          >
            {selected ? <MaterialIcons name="check" size={18} color={c.onSecondaryContainer} /> : null}
            <Text style={[type.titleSmall, { color: selected ? c.onSecondaryContainer : c.onSurface }]} numberOfLines={1}>
              {o.label}
            </Text>
          </Pressable>
        );
      })}
    </View>
  );
}

export function SectionLabel({ children }: { children: ReactNode }) {
  const c = useColors();
  return (
    <T variant="titleSmall" color={c.primary} style={{ paddingHorizontal: 8, paddingTop: 8 }}>
      {children}
    </T>
  );
}

const styles = StyleSheet.create({
  button: {
    height: 40,
    borderRadius: 20,
    borderWidth: 1,
    flexDirection: "row",
    alignItems: "center",
    justifyContent: "center",
    gap: 8,
  },
  iconButton: { width: 48, height: 48, borderRadius: 24, alignItems: "center", justifyContent: "center" },
  card: { borderRadius: 16, padding: 16 },
  switchTrack: { width: 52, height: 32, borderRadius: 16, borderWidth: 2, justifyContent: "center" },
  listRow: { flexDirection: "row", alignItems: "center", gap: 16, paddingHorizontal: 8, paddingVertical: 8, borderRadius: 12 },
  sheet: {
    position: "absolute",
    left: 0,
    right: 0,
    bottom: 0,
    maxHeight: "88%",
    borderTopLeftRadius: 28,
    borderTopRightRadius: 28,
  },
  handleRow: { alignItems: "center", paddingTop: 16, paddingBottom: 4 },
  handle: { width: 32, height: 4, borderRadius: 2, opacity: 0.4 },
  sheetHeader: { flexDirection: "row", alignItems: "center", paddingLeft: 24, paddingRight: 12, paddingTop: 4, paddingBottom: 8 },
  sheetBody: { paddingHorizontal: 16, paddingTop: 4, paddingBottom: 24, gap: 12 },
  sheetFooter: { paddingHorizontal: 16, paddingTop: 8, paddingBottom: 16, flexDirection: "row", gap: 8, justifyContent: "flex-end" },
  dialogScrim: { flex: 1, alignItems: "center", justifyContent: "center", padding: 24 },
  dialog: { borderRadius: 28, padding: 24, gap: 16, alignItems: "center", maxWidth: 340, width: "100%" },
  dialogActions: { alignSelf: "stretch", flexDirection: "row", justifyContent: "flex-end", gap: 8, paddingTop: 8 },
  field: { alignSelf: "stretch", height: 56, borderRadius: 4, borderWidth: 1, paddingHorizontal: 16 },
  snackbar: {
    position: "absolute",
    left: 16,
    right: 16,
    bottom: 96,
    minHeight: 48,
    borderRadius: 4,
    paddingHorizontal: 16,
    paddingVertical: 14,
    elevation: 6,
  },
  progressTrack: { height: 4, borderRadius: 2, overflow: "hidden", alignSelf: "stretch" },
  segmented: { flexDirection: "row", height: 40, borderRadius: 20, borderWidth: 1, overflow: "hidden" },
  segment: { flex: 1, flexDirection: "row", alignItems: "center", justifyContent: "center", gap: 6, paddingHorizontal: 8 },
});
