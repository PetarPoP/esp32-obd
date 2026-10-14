import { useMemo, useState } from "react";
import { Pressable, ScrollView, StyleSheet, View } from "react-native";

import { ChartCard, DialCard, StatusRow, TilesCard } from "../components/DashboardCards";
import { Button, Card, Icon, IconButton, Snackbar, T, type IconName } from "../components/m3";
import {
  defaultDashboard,
  pruneDashboard,
  selectedValueDefs,
  type DashboardCard,
  type ValueDef,
} from "../dashboard/dashboard";
import { useObdMonitor, type ObdMonitor } from "../hooks/useObdMonitor";
import { AdapterSheet } from "../sheets/AdapterSheet";
import { CodesSheet } from "../sheets/CodesSheet";
import { DashboardSheet } from "../sheets/DashboardSheet";
import { SettingsSheet } from "../sheets/SettingsSheet";
import { ToolsSheet } from "../sheets/ToolsSheet";
import { ValuesSheet } from "../sheets/ValuesSheet";
import { VehicleSheet } from "../sheets/VehicleSheet";
import type { SettingsStore } from "../state/settings";
import { useColors } from "../theme/theme";
import { catalogueFor, vehicleKey, vehicleName, type VehicleSelection, type VehicleValues } from "../types/vehicles";

type SheetName = "codes" | "vehicle" | "values" | "dashboard" | "settings" | "adapter" | "tools" | null;

/** "can11_500" -> { title: "CAN 500k", subtitle: "ISO 15765" } */
function busLabel(m: ObdMonitor, vehicle: VehicleSelection): { title: string; subtitle: string } {
  if (vehicle.kind === "smart_ed3") return { title: "CAN 500k", subtitle: "Smart ED" };
  if (m.probeStage) return { title: "Detecting…", subtitle: m.probeStage };
  const unverified = m.protocolInfo?.reason === "unverified";
  const id = (m.protocolInfo?.ok || unverified ? m.protocolInfo?.protocol : m.status?.protocol) ?? "";
  const match = /^can(11|29)_(\d+)$/.exec(id);
  if (match) {
    return { title: `CAN ${match[2]}k`, subtitle: unverified ? "Unverified" : match[1] === "11" ? "ISO 15765" : "29-bit IDs" };
  }
  if (m.protocolInfo && !m.protocolInfo.ok) return { title: "No answer", subtitle: "Ignition on?" };
  return { title: "Bus", subtitle: m.ready ? "Not detected" : "—" };
}

function linkLabel(m: ObdMonitor): { icon: IconName; title: string; subtitle: string } {
  switch (m.link) {
    case "ready":
      return { icon: "bluetooth-connected", title: "Connected", subtitle: m.rssi !== null ? `${m.rssi} dBm` : "OBD-ESP32" };
    case "pairing":
      return { icon: "bluetooth-searching", title: "Pairing…", subtitle: "Enter the PIN" };
    case "connecting":
      return { icon: "bluetooth-searching", title: "Searching…", subtitle: "OBD-ESP32" };
    default:
      return { icon: "bluetooth-disabled", title: "Not connected", subtitle: "Tap to connect" };
  }
}

export function HomeScreen({ store }: { store: SettingsStore }) {
  const c = useColors();
  const { settings, values } = store;
  const vehicle = settings.vehicle;
  const m = useObdMonitor({ vehicle, values });
  const [sheet, setSheet] = useState<SheetName>(null);

  const defs = useMemo(() => selectedValueDefs(vehicle, values), [vehicle, values]);
  const cards = useMemo(() => pruneDashboard(store.dashboard ?? defaultDashboard(defs), defs), [store.dashboard, defs]);
  const catalogue = catalogueFor(vehicle);
  const lookup = (key: string): ValueDef | undefined => {
    const d = defs.find((x) => x.key === key);
    if (d) return d;
    const spec = catalogue.find((x) => x.id === key);
    return spec ? { key, label: spec.label, unit: spec.unit, manufacturer: false } : undefined;
  };

  const pickVehicle = (v: VehicleSelection) => {
    store.update({ vehicle: v });
    // After choosing a car, ask which values to read (once per car).
    setSheet(settings.valuesChosen[vehicleKey(v)] ? null : "values");
  };

  const saveValues = (v: VehicleValues) => {
    store.setValues(v);
    setSheet(null);
    void m.applyValues(v);
    m.showToast(`Reading ${v.metrics.length + v.ext.length} values`);
  };

  const link = linkLabel(m);
  const bus = busLabel(m, vehicle);
  const codeCount = m.dtc && (!m.dtc.status || m.dtc.status === "ok")
    ? m.dtc.codes.length + (m.dtc.pending?.length ?? 0) + (m.dtc.permanent?.length ?? 0)
    : null;

  const stream = m.streaming
    ? { label: "Stop live data", icon: "stop" as const, bg: c.secondaryContainer, fg: c.onSecondaryContainer, onPress: m.stopStream }
    : m.ready
      ? { label: "Start live data", icon: "play-arrow" as const, bg: c.primary, fg: c.onPrimary, onPress: () => m.startStream() }
      : m.link === "disconnected"
        ? { label: "Connect", icon: "bluetooth-searching" as const, bg: c.primary, fg: c.onPrimary, onPress: () => void m.connect() }
        : { label: m.link === "pairing" ? "Pairing…" : "Searching…", icon: "bluetooth-searching" as const, bg: c.scHigh, fg: c.onSurfaceVariant, onPress: () => setSheet("adapter") };

  const renderCard = (card: DashboardCard) => {
    switch (card.type) {
      case "dial":
        return <DialCard key={card.id} arc={card.arc} center={card.center} footer={card.footer} telemetry={m.telemetry} history={m.history} lookup={lookup} />;
      case "status":
        return (
          <StatusRow key={card.id} linkIcon={link.icon} linkTitle={link.title} linkSubtitle={link.subtitle} linkActive={m.ready}
            onLink={() => (m.link === "disconnected" ? void m.connect() : setSheet("adapter"))}
            codeCount={codeCount} codesRead={codeCount !== null}
            onCodes={() => {
              if (!m.dtc && m.ready) m.readDtc();
              setSheet("codes");
            }}
            busTitle={bus.title} busSubtitle={bus.subtitle} onBus={() => setSheet("vehicle")} />
        );
      case "chart":
        return <ChartCard key={card.id} def={lookup(card.metric)} metric={card.metric} telemetry={m.telemetry} history={m.history} />;
      case "tiles":
        return <TilesCard key={card.id} metrics={card.metrics} telemetry={m.telemetry} lookup={lookup} />;
    }
  };

  return (
    <View style={[styles.root, { backgroundColor: c.surface }]}>
      <View style={styles.topBar}>
        <Pressable onPress={() => setSheet("vehicle")} style={({ pressed }) => [styles.vehicleButton, pressed && { opacity: 0.7 }]}>
          <T variant="titleLarge" numberOfLines={1} style={{ flexShrink: 1 }}>{vehicleName(vehicle)}</T>
          <Icon name="arrow-drop-down" />
        </Pressable>
        <View style={{ flex: 1 }} />
        <IconButton icon="settings" onPress={() => setSheet("settings")} />
      </View>

      <ScrollView contentContainerStyle={styles.content}>
        {defs.length === 0 ? (
          <Card style={{ gap: 12 }}>
            <T variant="titleMedium">Choose what to read</T>
            <T variant="bodySmall" color={c.onSurfaceVariant}>Pick the values for this car; the dashboard shows only those.</T>
            <Button icon="tune" label="Values to read" onPress={() => setSheet("values")} style={{ alignSelf: "flex-start" }} />
          </Card>
        ) : null}
        {cards.map(renderCard)}
        {m.ready && m.telemetryStatus === "degraded" ? (
          <T variant="bodySmall" color={c.error} style={{ paddingHorizontal: 8 }}>
            The car is not answering. Is the ignition on?
          </T>
        ) : null}
        <Pressable onPress={() => setSheet("dashboard")} style={styles.editLink}>
          <Icon name="edit" size={18} color={c.primary} />
          <T variant="titleSmall" color={c.primary}>Edit dashboard</T>
        </Pressable>
      </ScrollView>

      <View style={styles.bottom}>
        <Pressable onPress={stream.onPress} style={({ pressed }) => [styles.fab, { backgroundColor: stream.bg, opacity: pressed ? 0.9 : 1 }]}>
          <Icon name={stream.icon} color={stream.fg} />
          <T variant="titleMedium" color={stream.fg}>{stream.label}</T>
        </Pressable>
      </View>

      <Snackbar text={m.toast} />

      <CodesSheet visible={sheet === "codes"} onClose={() => setSheet(null)} m={m} lang={settings.dtcLang} lookup={lookup} />
      <VehicleSheet visible={sheet === "vehicle"} onClose={() => setSheet(null)} vehicle={vehicle} onPick={pickVehicle}
        onOpenValues={() => setSheet("values")} valueCount={defs.length} m={m} />
      <ValuesSheet visible={sheet === "values"} onClose={() => setSheet(null)} vehicle={vehicle} values={values} onSave={saveValues} m={m} />
      <DashboardSheet visible={sheet === "dashboard"} onClose={() => setSheet(null)} cards={cards} defs={defs}
        onSave={(next) => {
          store.setDashboard(next);
          setSheet(null);
        }}
        onReset={() => {
          store.setDashboard(null);
          setSheet(null);
          m.showToast("Dashboard reset to the default cards");
        }} />
      <SettingsSheet visible={sheet === "settings"} onClose={() => setSheet(null)} store={store} onOpen={setSheet} />
      <AdapterSheet visible={sheet === "adapter"} onClose={() => setSheet(null)} m={m} />
      <ToolsSheet visible={sheet === "tools"} onClose={() => setSheet(null)} m={m} />

    </View>
  );
}

const styles = StyleSheet.create({
  root: { flex: 1 },
  topBar: { height: 64, flexDirection: "row", alignItems: "center", paddingLeft: 8, paddingRight: 4 },
  vehicleButton: { height: 48, flexDirection: "row", alignItems: "center", gap: 4, paddingLeft: 12, paddingRight: 8, borderRadius: 24, maxWidth: "80%" },
  content: { paddingHorizontal: 16, paddingTop: 4, paddingBottom: 16, gap: 12 },
  editLink: { flexDirection: "row", alignItems: "center", justifyContent: "center", gap: 6, paddingVertical: 8 },
  bottom: { paddingHorizontal: 16, paddingTop: 8, paddingBottom: 12 },
  fab: {
    height: 56,
    borderRadius: 16,
    flexDirection: "row",
    alignItems: "center",
    justifyContent: "center",
    gap: 10,
    elevation: 3,
    shadowColor: "#000",
    shadowOpacity: 0.2,
    shadowRadius: 4,
    shadowOffset: { width: 0, height: 2 },
  },
});
