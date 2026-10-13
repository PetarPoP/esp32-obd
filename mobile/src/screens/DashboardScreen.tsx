import { useMemo } from "react";
import { ActivityIndicator, Pressable, ScrollView, StyleSheet, Text, View } from "react-native";
import { Picker } from "@react-native-picker/picker";

import { ActionButton } from "../components/ActionButton";
import { ConsoleView } from "../components/ConsoleView";
import { MetricCard } from "../components/MetricCard";
import { MetricCheckboxRow } from "../components/MetricCheckboxRow";
import { ProfileBrowser } from "../components/ProfileBrowser";
import { Section } from "../components/Section";
import { useObdMonitor } from "../hooks/useObdMonitor";
import { theme } from "../theme/theme";
import type { VehicleKind } from "../types/vehicles";

const SLEEP_OPTIONS: { label: string; seconds: number }[] = [
  { label: "Off", seconds: 0 },
  { label: "5 min", seconds: 300 },
  { label: "10 min", seconds: 600 },
  { label: "30 min", seconds: 1800 },
];

function formatNumber(value: number): string {
  if (Number.isInteger(value)) return String(value);
  const abs = Math.abs(value);
  return value.toFixed(abs >= 100 ? 1 : abs >= 1 ? 2 : 3);
}

export function DashboardScreen() {
  const m = useObdMonitor();

  const supported = useMemo(() => new Set(m.supportedMetrics), [m.supportedMetrics]);
  const catalogue = m.mode.metrics;
  const liveCards = m.selectedMetrics
    .map((id) => catalogue.find((spec) => spec.id === id))
    .filter((spec): spec is NonNullable<typeof spec> => spec !== undefined && m.telemetry[spec.id] !== undefined);
  const extCards =
    m.extValues.length > 0
      ? m.extValues
      : m.extOnDevice.map((key) => ({ key, name: key, unit: "", map: undefined, value: m.telemetry[key] }));

  return (
    <ScrollView contentContainerStyle={styles.container}>
      <Text style={styles.title}>OBD Mobile Monitor</Text>
      <Text style={styles.subtitle}>ESP32 · BLE · OBD-II, manufacturer PIDs, Smart ED</Text>

      <View style={styles.row}>
        <Text style={[styles.badge, m.connected ? styles.ok : styles.warn]}>
          {m.connected ? "Connected" : m.connecting ? "Searching for OBD-ESP32…" : "Disconnected"}
        </Text>
        {!m.connected && !m.connecting ? (
          <Pressable onPress={() => void m.connect()} style={styles.smallButton}>
            <Text style={styles.smallButtonText}>Connect</Text>
          </Pressable>
        ) : null}
        {m.connecting ? <ActivityIndicator color={theme.colors.accent} /> : null}
      </View>

      <Section title="Vehicle" subtitle={m.mode.label}>
        <View style={styles.pickerBox}>
          <Picker
            selectedValue={m.mode.id}
            onValueChange={(v) => m.selectMode(v as VehicleKind)}
            dropdownIconColor={theme.colors.textPrimary}
            style={styles.picker}
          >
            {m.modes.map((mode) => (
              <Picker.Item key={mode.id} label={mode.label} value={mode.id} color={theme.colors.textPrimary} />
            ))}
          </Picker>
        </View>
        <Text style={styles.text}>{m.mode.notes}</Text>
        {m.probeStage ? <Text style={styles.text}>Detecting protocol: {m.probeStage}…</Text> : null}
        {m.protocolInfo ? (
          <Text style={styles.text}>
            Protocol: {m.protocolInfo.ok ? m.protocolInfo.protocol : `not found (${m.protocolInfo.reason ?? "no answer"})`}
            {m.protocolInfo.vin ? ` · VIN ${m.protocolInfo.vin}` : ""}
          </Text>
        ) : m.status?.protocol ? (
          <Text style={styles.text}>Protocol: {m.status.protocol}</Text>
        ) : null}
        <Text style={styles.text}>
          PID support: {m.capabilitiesConfirmed ? `${m.supportedMetrics.length} values confirmed by the car` : "not probed yet"}
        </Text>
        {m.mode.id !== "smart_ed3" ? (
          <ActionButton label="Detect protocol again" onPress={() => void m.sendCommand("detect_protocol", { protocol: "auto" })} />
        ) : null}
      </Section>

      {m.mode.id === "profile" ? (
        <Section title="Make & model" subtitle="Manufacturer-specific values (OBDb)">
          <ProfileBrowser
            connected={m.connected}
            extLoad={m.extLoad}
            onLoad={(signals) => void m.loadExtSignals(signals)}
            onClear={() => void m.clearExtSignals()}
          />
        </Section>
      ) : null}

      <Section title="Metrics to show" subtitle={`${m.selectedMetrics.length} selected`} initiallyOpen={false}>
        <View style={styles.toolbar}>
          <Pressable
            onPress={() =>
              m.setSelectedMetrics(
                catalogue.filter((s) => !m.capabilitiesConfirmed || supported.has(s.id)).map((s) => s.id)
              )
            }
          >
            <Text style={styles.link}>{m.capabilitiesConfirmed ? "Select all supported" : "Select all"}</Text>
          </Pressable>
          <Pressable onPress={() => m.setSelectedMetrics([])}>
            <Text style={styles.link}>Clear</Text>
          </Pressable>
        </View>
        {catalogue.map((spec) => {
          const unsupported = m.capabilitiesConfirmed && m.mode.id !== "smart_ed3" && !supported.has(spec.id);
          return (
            <MetricCheckboxRow
              key={spec.id}
              label={`${spec.label}${spec.unit ? ` (${spec.unit})` : ""}`}
              sublabel={`${spec.pid === "Smart ED" ? spec.id : `PID ${spec.pid} · ${spec.id}`}${unsupported ? " · not supported by this car" : ""}`}
              checked={m.selectedMetrics.includes(spec.id)}
              onToggle={() => m.toggleMetric(spec.id)}
            />
          );
        })}
      </Section>

      <Section title="Live values" subtitle={m.status?.stream_active ? "streaming" : "idle"}>
        <View style={styles.actionsRow}>
          <View style={styles.flex}>
            <ActionButton label={m.status?.stream_active ? "Restart stream" : "Start stream"} onPress={m.startStream} />
          </View>
          <View style={styles.flex}>
            <ActionButton label="Stop stream" onPress={m.stopStream} />
          </View>
        </View>
        {liveCards.length === 0 && extCards.every((c) => c.value === undefined) ? (
          <Text style={styles.text}>
            No values yet. Pick metrics and press Start stream; a card appears once the car returns a value.
            {m.telemetryStatus === "degraded" ? " The car is not answering (degraded)." : ""}
          </Text>
        ) : null}
        <View style={styles.grid}>
          {liveCards.map((spec) => (
            <MetricCard
              key={spec.id}
              label={spec.label}
              value={`${formatNumber(m.telemetry[spec.id])} ${spec.unit}`}
              emphasis={spec.id === "rpm" || spec.id === "soc_pct"}
            />
          ))}
          {extCards
            .filter((c) => c.value !== undefined)
            .map((c) => (
              <MetricCard
                key={c.key}
                label={c.name}
                value={c.map?.[String(c.value)] ?? `${formatNumber(c.value as number)} ${c.unit}`}
              />
            ))}
        </View>
      </Section>

      <Section title="Diagnostics" subtitle="Trouble codes, readiness, monitor tests">
        <View style={styles.actionsRow}>
          <View style={styles.flex}>
            <ActionButton label="Read DTC" onPress={() => void m.sendCommand("read_dtc")} />
          </View>
          <View style={styles.flex}>
            <ActionButton label="Freeze frame" onPress={m.readFreezeFrame} />
          </View>
        </View>
        {m.dtc ? (
          <View style={styles.box}>
            <Text style={styles.boxTitle}>Stored: {m.dtc.codes.length ? m.dtc.codes.join(", ") : "none"}</Text>
            <Text style={styles.text}>Pending: {m.dtc.pending?.length ? m.dtc.pending.join(", ") : "none"}</Text>
            <Text style={styles.text}>Permanent: {m.dtc.permanent?.length ? m.dtc.permanent.join(", ") : "none"}</Text>
            {m.dtc.status && m.dtc.status !== "ok" ? <Text style={styles.warnText}>Status: {m.dtc.status}</Text> : null}
          </View>
        ) : null}
        {m.freezeFrame ? (
          <View style={styles.box}>
            <Text style={styles.boxTitle}>
              Freeze frame {m.freezeFrame.dtc ? `for ${String(m.freezeFrame.dtc)}` : `(${String(m.freezeFrame.status)})`}
            </Text>
            {Object.entries(m.freezeFrame)
              .filter(([k]) => k !== "dtc" && k !== "status")
              .map(([k, v]) => (
                <Text key={k} style={styles.text}>
                  {catalogue.find((s) => s.id === k)?.label ?? k}: {String(v)}
                </Text>
              ))}
          </View>
        ) : null}

        <View style={styles.actionsRow}>
          <View style={styles.flex}>
            <ActionButton label="Readiness" onPress={() => void m.sendCommand("read_readiness")} />
          </View>
          <View style={styles.flex}>
            <ActionButton label={m.monitorRunning ? "Reading tests…" : "Monitor tests"} onPress={m.readMonitorTests} />
          </View>
        </View>
        {m.readiness ? (
          <View style={styles.box}>
            {m.readiness.status === "ok" ? (
              <>
                <Text style={styles.boxTitle}>
                  MIL {m.readiness.mil ? "ON" : "off"} · {m.readiness.dtc_count} DTC · {m.readiness.ignition} ignition
                </Text>
                <Text style={styles.okText}>Complete: {m.readiness.complete?.join(", ") || "—"}</Text>
                <Text style={m.readiness.incomplete?.length ? styles.warnText : styles.text}>
                  Incomplete: {m.readiness.incomplete?.join(", ") || "none — ready for inspection"}
                </Text>
              </>
            ) : (
              <Text style={styles.warnText}>Readiness: {m.readiness.status}</Text>
            )}
          </View>
        ) : null}
        {m.monitorTests.length > 0 ? (
          <View style={styles.box}>
            <Text style={styles.boxTitle}>
              Monitor tests: {m.monitorTests.filter((t) => !t.pass).length} failed of {m.monitorTests.length}
            </Text>
            {m.monitorTests.map((t, i) => (
              <Text key={i} style={t.pass ? styles.text : styles.warnText}>
                {`MID ${t.mid}/TID ${t.tid}: ${
                  t.value !== undefined
                    ? `${formatNumber(t.value)} ${t.unit ?? ""} (${formatNumber(t.min ?? 0)}–${formatNumber(t.max ?? 0)})`
                    : `${t.raw} (${t.raw_min}–${t.raw_max})`
                } ${t.pass ? "PASS" : "FAIL"}`}
              </Text>
            ))}
          </View>
        ) : null}

        <View style={styles.actionsRow}>
          <View style={styles.flex}>
            <ActionButton label="Vehicle info" onPress={() => void m.sendCommand("vehicle_info")} />
          </View>
          <View style={styles.flex}>
            <ActionButton label="Clear DTC…" onPress={() => void m.sendCommand("clear_dtc")} />
          </View>
        </View>
        {m.vehicleInfo ? (
          <View style={styles.box}>
            <Text style={styles.boxTitle}>VIN: {m.vehicleInfo.vin || "not reported"}</Text>
            <Text style={styles.text}>Calibration IDs: {m.vehicleInfo.cal_ids?.join(", ") || "—"}</Text>
            <Text style={styles.text}>Control units: {m.vehicleInfo.ecu_names?.join(", ") || "—"}</Text>
          </View>
        ) : null}
      </Section>

      <Section title="Device" subtitle={m.device ? `firmware ${m.device.fw}` : "ESP32"} initiallyOpen={false}>
        {m.device ? (
          <>
            <Text style={styles.text}>
              Woke up by: {m.device.wake} · BLE MTU {m.device.mtu} · manufacturer signals {m.device.ext_pids}/
              {m.device.ext_pids_max}
            </Text>
            <Text style={styles.text}>
              Sleep when the car is off and no phone is connected:{" "}
              {m.device.sleep_after_s === 0 ? "never" : `after ${Math.round(m.device.sleep_after_s / 60)} min`}
            </Text>
          </>
        ) : null}
        <View style={styles.box}>
          <Text style={styles.boxTitle}>Firmware update</Text>
          <Text style={styles.text}>
            {m.firmware.message ||
              `Installed: ${m.device?.fw ?? "unknown"}. Updates are downloaded from GitHub and sent to the ESP over Bluetooth.`}
          </Text>
          {m.firmware.phase === "downloading" || m.firmware.phase === "uploading" || m.firmware.phase === "installing" ? (
            <View style={styles.progressTrack}>
              <View style={[styles.progressFill, { width: `${Math.round(m.firmware.progress * 100)}%` }]} />
            </View>
          ) : null}
          <View style={styles.actionsRow}>
            <View style={styles.flex}>
              <ActionButton
                label="Check for update"
                onPress={() => {
                  const busy = ["downloading", "uploading", "installing"].includes(m.firmware.phase);
                  if (!busy) void m.checkFirmware();
                }}
              />
            </View>
            {m.firmware.phase === "available" ? (
              <View style={styles.flex}>
                <ActionButton label={`Install ${m.firmware.latest?.version ?? ""}`} onPress={() => void m.installFirmware()} />
              </View>
            ) : null}
          </View>
          <Text style={styles.text}>Keep the phone close to the ESP and the app open until the ESP restarts.</Text>
        </View>
        <View style={styles.actionsRow}>
          {SLEEP_OPTIONS.map((o) => (
            <Pressable key={o.label} onPress={() => m.setSleepAfter(o.seconds)} style={styles.smallButton}>
              <Text style={styles.smallButtonText}>{o.label}</Text>
            </Pressable>
          ))}
        </View>
        <View style={styles.actionsRow}>
          <View style={styles.flex}>
            <ActionButton label="Bus diagnostic" onPress={() => void m.sendCommand("bus_diag")} />
          </View>
          <View style={styles.flex}>
            <ActionButton label="Scan baud (8 s)" onPress={() => void m.sendCommand("bus_scan")} />
          </View>
        </View>
        {m.busScanRunning || m.busScanResults.length > 0 ? (
          <View style={styles.box}>
            <Text style={styles.boxTitle}>Bus scan {m.busScanRunning ? "(running…)" : "(done)"}</Text>
            {m.busScanResults.map((r) => (
              <Text key={r.baud_kbps} style={styles.text}>
                {`${r.baud_kbps} kbps: frames=${r.frames}, bus_errors=${r.bus_errors_delta}${r.first_id ? `, first=${r.first_id}` : ""} -> ${
                  r.frames > 0 ? "OK" : r.bus_errors_delta > 100 ? "wrong baud" : "silent"
                }`}
              </Text>
            ))}
          </View>
        ) : null}
      </Section>

      <Section title="BLE console" initiallyOpen={false}>
        <View style={styles.toolbar}>
          <Pressable onPress={() => void m.exportConsoleLog()}>
            <Text style={styles.link}>Export</Text>
          </Pressable>
          <Pressable onPress={m.clearConsole}>
            <Text style={styles.link}>Clear</Text>
          </Pressable>
        </View>
        <ConsoleView lines={m.consoleLines} />
      </Section>

      <Text style={styles.footer}>Last response: {m.lastAck}</Text>
    </ScrollView>
  );
}

const styles = StyleSheet.create({
  container: { padding: theme.spacing.lg, gap: theme.spacing.md },
  title: { color: theme.colors.textPrimary, fontSize: 28, fontWeight: "800" },
  subtitle: { color: theme.colors.textSecondary, fontSize: 14 },
  row: { flexDirection: "row", alignItems: "center", gap: theme.spacing.sm },
  badge: { fontWeight: "700", fontSize: 14 },
  ok: { color: theme.colors.accent },
  warn: { color: theme.colors.warning },
  okText: { color: theme.colors.accent, fontSize: 13 },
  warnText: { color: theme.colors.warning, fontSize: 13 },
  text: { color: theme.colors.textSecondary, fontSize: 13 },
  pickerBox: {
    backgroundColor: theme.colors.background,
    borderRadius: theme.radius.md,
    borderWidth: 1,
    borderColor: theme.colors.border,
    overflow: "hidden",
  },
  picker: { color: theme.colors.textPrimary },
  toolbar: { flexDirection: "row", gap: theme.spacing.md },
  link: { color: theme.colors.accent, fontWeight: "700", fontSize: 13 },
  grid: { flexDirection: "row", flexWrap: "wrap", gap: theme.spacing.sm },
  actionsRow: { flexDirection: "row", gap: theme.spacing.sm, flexWrap: "wrap" },
  flex: { flex: 1 },
  box: {
    backgroundColor: theme.colors.background,
    borderRadius: theme.radius.md,
    borderWidth: 1,
    borderColor: theme.colors.border,
    padding: theme.spacing.sm,
    gap: 4,
  },
  boxTitle: { color: theme.colors.textPrimary, fontWeight: "700", fontSize: 14 },
  progressTrack: { height: 8, borderRadius: 4, backgroundColor: theme.colors.border, overflow: "hidden" },
  progressFill: { height: 8, backgroundColor: theme.colors.accent },
  smallButton: {
    backgroundColor: theme.colors.background,
    borderColor: theme.colors.border,
    borderWidth: 1,
    borderRadius: theme.radius.sm,
    paddingHorizontal: 12,
    paddingVertical: 6,
  },
  smallButtonText: { color: theme.colors.textPrimary, fontSize: 13, fontWeight: "600" },
  footer: { color: theme.colors.textSecondary, fontSize: 13, marginTop: theme.spacing.sm },
});
