import { useEffect, useMemo, useState } from "react";
import { ActivityIndicator, View } from "react-native";

import { Button, Checkbox, Field, ListRow, Segmented, Sheet, Switch, T } from "../components/m3";
import { APP_CONFIG } from "../config";
import type { ObdMonitor } from "../hooks/useObdMonitor";
import {
  flattenSignals,
  listModels,
  loadProfile,
  toStoredSignal,
  yearsLabel,
  type ExtSignalSelection,
  type VehicleProfileData,
} from "../services/profileRepository";
import { useColors } from "../theme/theme";
import { catalogueFor, vehicleName, type StoredExtSignal, type VehicleSelection, type VehicleValues } from "../types/vehicles";

const MAX_LISTED = 150;

/** Choose which values to read from the car; only these are polled and shown. */
export function ValuesSheet({
  visible,
  onClose,
  vehicle,
  values,
  onSave,
  m,
}: {
  visible: boolean;
  onClose: () => void;
  vehicle: VehicleSelection;
  values: VehicleValues;
  onSave: (v: VehicleValues) => void;
  m: ObdMonitor;
}) {
  const c = useColors();
  const [tab, setTab] = useState<"standard" | "manufacturer">("standard");
  const [metrics, setMetrics] = useState<string[]>(values.metrics);
  const [ext, setExt] = useState<StoredExtSignal[]>(values.ext);
  const [query, setQuery] = useState("");
  const [onlySupported, setOnlySupported] = useState(true);
  const [profile, setProfile] = useState<VehicleProfileData | null>(null);
  const [profileError, setProfileError] = useState<string | null>(null);
  const [loading, setLoading] = useState(false);

  // Start from the saved selection every time the sheet opens.
  useEffect(() => {
    if (!visible) return;
    setMetrics(values.metrics);
    setExt(values.ext);
    setQuery("");
    setTab("standard");
    // eslint-disable-next-line react-hooks/exhaustive-deps
  }, [visible]);

  const profileFile = vehicle.kind === "profile" ? vehicle.file : null;
  useEffect(() => {
    if (!visible || !profileFile || vehicle.kind !== "profile") return;
    const entry = listModels(vehicle.make).find((e) => e.file === profileFile);
    if (!entry) return;
    let alive = true;
    setLoading(true);
    setProfileError(null);
    loadProfile(entry)
      .then((p) => alive && setProfile(p))
      .catch((e: Error) => alive && setProfileError(e.message))
      .finally(() => alive && setLoading(false));
    return () => {
      alive = false;
    };
    // eslint-disable-next-line react-hooks/exhaustive-deps
  }, [visible, profileFile]);

  const catalogue = catalogueFor(vehicle);
  const supported = useMemo(() => new Set(m.supportedMetrics), [m.supportedMetrics]);
  const filterBySupport = vehicle.kind !== "smart_ed3" && m.capabilitiesConfirmed && onlySupported;
  const q = query.trim().toLowerCase();

  const standardRows = catalogue.filter(
    (s) =>
      (!filterBySupport || supported.has(s.id) || metrics.includes(s.id)) &&
      (!q || s.label.toLowerCase().includes(q) || s.id.includes(q))
  );

  const signals = useMemo<ExtSignalSelection[]>(() => (profile ? flattenSignals(profile) : []), [profile]);
  const extKeys = new Set(ext.map((s) => s.key));
  const signalRows = signals.filter(
    (s) => !q || s.name.toLowerCase().includes(q) || s.key.toLowerCase().includes(q) || s.path.toLowerCase().includes(q)
  );

  const toggleMetric = (id: string) =>
    setMetrics((prev) => (prev.includes(id) ? prev.filter((x) => x !== id) : [...prev, id]));
  const toggleSignal = (s: ExtSignalSelection) =>
    setExt((prev) => {
      if (prev.some((x) => x.key === s.key)) return prev.filter((x) => x.key !== s.key);
      if (prev.length >= APP_CONFIG.maxExtSignals) return prev;
      return [...prev, toStoredSignal(s)];
    });

  const total = metrics.length + (vehicle.kind === "profile" ? ext.length : 0);

  return (
    <Sheet
      visible={visible}
      title="Values to read"
      onClose={onClose}
      footer={
        <>
          <T variant="bodySmall" color={c.onSurfaceVariant} style={{ flex: 1, alignSelf: "center" }}>
            {total} selected
          </T>
          <Button kind="text" label="Clear" onPress={() => (tab === "standard" ? setMetrics([]) : setExt([]))} />
          <Button
            label="Save"
            onPress={() => onSave({ metrics, ext: vehicle.kind === "profile" ? ext : [] })}
            disabled={total === 0 || m.extLoad.running}
          />
        </>
      }
    >
      <T variant="bodySmall" color={c.onSurfaceVariant} style={{ paddingHorizontal: 8 }}>
        {vehicleName(vehicle)}: only the values you tick are read from the car and shown on the dashboard. Fewer values
        update faster.
      </T>
      {vehicle.kind === "profile" ? (
        <Segmented
          options={[
            { value: "standard", label: `Standard (${metrics.length})` },
            { value: "manufacturer", label: `Manufacturer (${ext.length})` },
          ]}
          value={tab}
          onChange={(v) => {
            setTab(v);
            setQuery("");
          }}
        />
      ) : null}
      <Field value={query} onChangeText={setQuery} placeholder={tab === "standard" ? "Search (rpm, coolant, fuel…)" : "Search (battery, oil, soc, tire…)"} />

      {tab === "standard" ? (
        <>
          {vehicle.kind !== "smart_ed3" ? (
            <ListRow
              title="Only values this car supports"
              subtitle={m.capabilitiesConfirmed ? `${m.supportedMetrics.length} reported by the car` : "Connect to the car to find out"}
              trailing={<Switch value={onlySupported} onChange={setOnlySupported} />}
              disabled={!m.capabilitiesConfirmed}
            />
          ) : null}
          <View>
            {standardRows.map((s) => (
              <ListRow
                key={s.id}
                title={`${s.label}${s.unit ? ` (${s.unit})` : ""}`}
                subtitle={`${s.pid === "Smart ED" ? s.id : `PID ${s.pid}`}${
                  m.capabilitiesConfirmed && vehicle.kind !== "smart_ed3" && !supported.has(s.id) ? " · not supported by this car" : ""
                }`}
                leading={<Checkbox checked={metrics.includes(s.id)} />}
                onPress={() => toggleMetric(s.id)}
              />
            ))}
          </View>
        </>
      ) : (
        <>
          {loading ? <ActivityIndicator color={c.primary} /> : null}
          {profileError ? <T variant="bodySmall" color={c.error}>Could not download the profile: {profileError}</T> : null}
          {profile ? (
            <T variant="bodySmall" color={c.onSurfaceVariant} style={{ paddingHorizontal: 8 }}>
              {signals.length} values from {profile.source} ({profile.license}), community data. Up to {APP_CONFIG.maxExtSignals} fit
              on the adapter.
            </T>
          ) : null}
          {/* Signals already chosen stay visible even when the profile cannot be downloaded. */}
          {!profile && ext.length > 0
            ? ext.map((s) => (
                <ListRow key={s.key} title={s.name} subtitle={s.path || s.key} leading={<Checkbox checked />}
                  onPress={() => setExt((prev) => prev.filter((x) => x.key !== s.key))} />
              ))
            : null}
          <View>
            {signalRows.slice(0, MAX_LISTED).map((s) => {
              const years = yearsLabel(s.command);
              return (
                <ListRow
                  key={s.key}
                  title={`${s.name}${s.unit ? ` (${s.unit})` : ""}`}
                  subtitle={`${s.path ? `${s.path} · ` : ""}${s.command.tx} ${s.command.svc} ${s.command.did}${years ? ` · ${years}` : ""}`}
                  leading={<Checkbox checked={extKeys.has(s.key)} />}
                  onPress={() => toggleSignal(s)}
                />
              );
            })}
          </View>
          {signalRows.length > MAX_LISTED ? (
            <T variant="bodySmall" color={c.onSurfaceVariant} style={{ paddingHorizontal: 8 }}>
              Showing {MAX_LISTED} of {signalRows.length}; search to narrow it down.
            </T>
          ) : null}
        </>
      )}
    </Sheet>
  );
}
