import { useEffect, useMemo, useState } from "react";
import { ActivityIndicator, Pressable, StyleSheet, Text, TextInput, View } from "react-native";
import { Picker } from "@react-native-picker/picker";

import { APP_CONFIG } from "../config";
import {
  flattenSignals,
  listMakes,
  listModels,
  loadProfile,
  yearsLabel,
  type ExtSignalSelection,
  type ProfileIndexEntry,
  type VehicleProfileData,
} from "../services/profileRepository";
import type { ExtLoadState } from "../hooks/useObdMonitor";
import { theme } from "../theme/theme";
import { ActionButton } from "./ActionButton";
import { MetricCheckboxRow } from "./MetricCheckboxRow";

type Props = {
  connected: boolean;
  extLoad: ExtLoadState;
  onLoad: (signals: ExtSignalSelection[]) => void;
  onClear: () => void;
};

const MAX_LISTED = 150;

/** Pick make and model, choose manufacturer signals and load them to the ESP. */
export function ProfileBrowser({ connected, extLoad, onLoad, onClear }: Props) {
  const makes = useMemo(() => listMakes(), []);
  const [make, setMake] = useState<string>("");
  const models = useMemo(() => (make ? listModels(make) : []), [make]);
  const [entry, setEntry] = useState<ProfileIndexEntry | null>(null);
  const [profile, setProfile] = useState<VehicleProfileData | null>(null);
  const [loading, setLoading] = useState(false);
  const [error, setError] = useState<string | null>(null);
  const [query, setQuery] = useState("");
  const [chosen, setChosen] = useState<Set<string>>(new Set());

  useEffect(() => {
    setEntry(null);
    setProfile(null);
    setChosen(new Set());
  }, [make]);

  useEffect(() => {
    if (!entry) return;
    let alive = true;
    setLoading(true);
    setError(null);
    setProfile(null);
    setChosen(new Set());
    loadProfile(entry)
      .then((p) => alive && setProfile(p))
      .catch((e: Error) => alive && setError(e.message))
      .finally(() => alive && setLoading(false));
    return () => {
      alive = false;
    };
  }, [entry]);

  const signals = useMemo(() => (profile ? flattenSignals(profile) : []), [profile]);
  const filtered = useMemo(() => {
    const q = query.trim().toLowerCase();
    if (!q) return signals;
    return signals.filter(
      (s) => s.name.toLowerCase().includes(q) || s.key.toLowerCase().includes(q) || s.path.toLowerCase().includes(q)
    );
  }, [signals, query]);

  const toggle = (key: string) =>
    setChosen((prev) => {
      const next = new Set(prev);
      if (next.has(key)) next.delete(key);
      else if (next.size < APP_CONFIG.maxExtSignals) next.add(key);
      return next;
    });

  const selectVisible = () =>
    setChosen((prev) => {
      const next = new Set(prev);
      for (const s of filtered) {
        if (next.size >= APP_CONFIG.maxExtSignals) break;
        next.add(s.key);
      }
      return next;
    });

  return (
    <View style={styles.container}>
      <View style={styles.pickerBox}>
        <Picker selectedValue={make} onValueChange={(v) => setMake(String(v))} style={styles.picker} dropdownIconColor={theme.colors.textPrimary}>
          <Picker.Item label={`Make (${makes.length})`} value="" color={theme.colors.textSecondary} />
          {makes.map((m) => (
            <Picker.Item key={m} label={m} value={m} color={theme.colors.textPrimary} />
          ))}
        </Picker>
      </View>
      {make ? (
        <View style={styles.pickerBox}>
          <Picker
            selectedValue={entry?.file ?? ""}
            onValueChange={(v) => setEntry(models.find((m) => m.file === v) ?? null)}
            style={styles.picker}
            dropdownIconColor={theme.colors.textPrimary}
          >
            <Picker.Item label={`Model (${models.length})`} value="" color={theme.colors.textSecondary} />
            {models.map((m) => (
              <Picker.Item key={m.file} label={`${m.model} · ${m.signals} signals`} value={m.file} color={theme.colors.textPrimary} />
            ))}
          </Picker>
        </View>
      ) : null}

      {loading ? <ActivityIndicator color={theme.colors.accent} /> : null}
      {error ? <Text style={styles.error}>Could not load profile: {error}</Text> : null}

      {profile ? (
        <>
          <Text style={styles.hint}>
            {signals.length} signals · source {profile.source} ({profile.license}). Community data: check values
            before relying on them. Up to {APP_CONFIG.maxExtSignals} signals fit on the ESP.
          </Text>
          <TextInput
            value={query}
            onChangeText={setQuery}
            placeholder="Search (e.g. battery, oil, soc, tire)"
            placeholderTextColor={theme.colors.textSecondary}
            style={styles.search}
          />
          <View style={styles.toolbar}>
            <Pressable onPress={selectVisible}>
              <Text style={styles.link}>Select shown</Text>
            </Pressable>
            <Pressable onPress={() => setChosen(new Set())}>
              <Text style={styles.link}>Clear</Text>
            </Pressable>
            <Text style={styles.count}>
              {chosen.size}/{APP_CONFIG.maxExtSignals} selected
            </Text>
          </View>
          {filtered.slice(0, MAX_LISTED).map((s) => {
            const years = yearsLabel(s.command);
            return (
              <MetricCheckboxRow
                key={s.key}
                label={`${s.name}${s.unit ? ` (${s.unit})` : ""}`}
                sublabel={`${s.path ? `${s.path} · ` : ""}${s.command.tx}→${s.command.svc} ${s.command.did}${years ? ` · ${years}` : ""}`}
                checked={chosen.has(s.key)}
                onToggle={() => toggle(s.key)}
              />
            );
          })}
          {filtered.length > MAX_LISTED ? (
            <Text style={styles.hint}>Showing {MAX_LISTED} of {filtered.length}; refine the search.</Text>
          ) : null}
          <ActionButton
            label={
              extLoad.running
                ? `Loading ${extLoad.sent}/${extLoad.total}…`
                : `Load ${chosen.size} signal(s) to ESP`
            }
            onPress={() => {
              if (!connected || extLoad.running || chosen.size === 0) return;
              onLoad(signals.filter((s) => chosen.has(s.key)));
            }}
          />
        </>
      ) : null}
      {extLoad.failed.length > 0 && !extLoad.running ? (
        <Text style={styles.error}>Rejected by ESP: {extLoad.failed.join(", ")}</Text>
      ) : null}
      <ActionButton label="Remove all manufacturer signals from ESP" onPress={onClear} />
    </View>
  );
}

const styles = StyleSheet.create({
  container: { gap: theme.spacing.sm },
  pickerBox: {
    backgroundColor: theme.colors.background,
    borderRadius: theme.radius.md,
    borderWidth: 1,
    borderColor: theme.colors.border,
    overflow: "hidden",
  },
  picker: { color: theme.colors.textPrimary },
  search: {
    backgroundColor: theme.colors.background,
    borderRadius: theme.radius.md,
    borderWidth: 1,
    borderColor: theme.colors.border,
    color: theme.colors.textPrimary,
    paddingHorizontal: 12,
    paddingVertical: 8,
  },
  toolbar: { flexDirection: "row", alignItems: "center", gap: theme.spacing.md },
  link: { color: theme.colors.accent, fontWeight: "700", fontSize: 13 },
  count: { color: theme.colors.textSecondary, fontSize: 13, marginLeft: "auto" },
  hint: { color: theme.colors.textSecondary, fontSize: 12 },
  error: { color: theme.colors.danger, fontSize: 13 },
});
