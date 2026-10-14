import { ScrollView, StyleSheet, View } from "react-native";

import { Button, Card, ProgressBar, Sheet, T } from "../components/m3";
import { formatNumber } from "../dashboard/dashboard";
import type { ObdMonitor } from "../hooks/useObdMonitor";
import { useColors } from "../theme/theme";

type Tool = { name: string; desc: string; button: string; run: () => void; busy?: boolean; result?: string | null };

export function ToolsSheet({ visible, onClose, m }: { visible: boolean; onClose: () => void; m: ObdMonitor }) {
  const c = useColors();
  const scan = m.busScanResults.filter(Boolean);
  const failedTests = m.monitorTests.filter((t) => !t.pass).length;

  const tools: Tool[] = [
    {
      name: "PID probe",
      desc: "Ask the car which standard PIDs it supports",
      button: "Probe",
      run: () => void m.sendCommand("probe_capabilities"),
      result: m.capabilitiesConfirmed ? `${m.supportedMetrics.length} values supported` : null,
    },
    {
      name: "Detect protocol",
      desc: "CAN 11/29-bit at 500/250 kbps",
      button: "Detect",
      run: () => void m.sendCommand("detect_protocol", { protocol: "auto" }),
      busy: m.probeStage !== null,
      result:
        m.probeStage ??
        (m.protocolInfo
          ? m.protocolInfo.ok
            ? m.protocolInfo.protocol
            : `not found (${m.protocolInfo.reason ?? "?"})${
                m.protocolInfo.probe
                  ? ` · tx_error ${m.protocolInfo.probe.tx_error}, tx_failed ${m.protocolInfo.probe.tx_failed}, bus_error ${m.protocolInfo.probe.bus_error}${m.protocolInfo.probe.bus_off ? ", bus-off" : ""}`
                  : ""
              }`
          : null),
    },
    {
      name: "Bus check",
      desc: "CAN controller state and error counters",
      button: "Check",
      run: () => void m.sendCommand("bus_diag"),
      result: m.busDiag,
    },
    {
      name: "Baud scan",
      desc: "Listen at 100–1000 kbps, about 8 s",
      button: "Scan",
      run: () => void m.sendCommand("bus_scan"),
      busy: m.busScanRunning,
      result: scan.length
        ? scan.map((r) => `${r.baud_kbps}k: ${r.frames > 0 ? `${r.frames} frames` : r.bus_errors_delta > 100 ? "wrong baud" : "silent"}`).join(" · ")
        : null,
    },
    {
      name: "Monitor tests (Mode 06)",
      desc: "On-board test results with limits",
      button: "Read",
      run: m.readMonitorTests,
      busy: m.monitorRunning,
      result: m.monitorTests.length ? `${failedTests} failed of ${m.monitorTests.length}` : null,
    },
    {
      name: "Vehicle info",
      desc: "VIN, calibration IDs, control units",
      button: "Read",
      run: () => void m.sendCommand("vehicle_info"),
      result: m.vehicleInfo
        ? `VIN ${m.vehicleInfo.vin || "—"} · CAL ${m.vehicleInfo.cal_ids?.join(", ") || "—"} · ${m.vehicleInfo.ecu_names?.join(", ") || "—"}`
        : null,
    },
  ];

  return (
    <Sheet visible={visible} title="Developer tools" onClose={onClose}>
      {tools.map((t) => (
        <Card key={t.name} level="mid" style={{ gap: 10, paddingVertical: 12, paddingRight: 12 }}>
          <View style={styles.toolHead}>
            <View style={{ flex: 1, minWidth: 0 }}>
              <T variant="titleMedium">{t.name}</T>
              <T variant="labelSmall" color={c.onSurfaceVariant}>{t.desc}</T>
            </View>
            <Button kind="tonal" label={t.button} onPress={t.run} disabled={!m.ready} />
          </View>
          {t.busy ? <ProgressBar progress={0.5} /> : null}
          {t.result ? <T variant="mono" color={c.primary}>{t.result}</T> : null}
        </Card>
      ))}

      {m.monitorTests.length > 0 ? (
        <Card level="mid" style={{ gap: 2 }}>
          {m.monitorTests.map((t, i) => (
            <T key={i} variant="mono" color={t.pass ? c.onSurfaceVariant : c.error}>
              {`MID ${t.mid}/TID ${t.tid}: ${
                t.value !== undefined
                  ? `${formatNumber(t.value)} ${t.unit ?? ""} (${formatNumber(t.min ?? 0)}–${formatNumber(t.max ?? 0)})`
                  : `${t.raw} (${t.raw_min}–${t.raw_max})`
              } ${t.pass ? "PASS" : "FAIL"}`}
            </T>
          ))}
        </Card>
      ) : null}

      <View style={styles.logHead}>
        <T variant="titleSmall" style={{ flex: 1 }}>BLE log</T>
        <Button kind="text" label="Clear" onPress={m.clearConsole} />
        <Button kind="text" icon="share" label="Export" onPress={() => void m.exportConsoleLog()} />
      </View>
      <View style={[styles.log, { backgroundColor: c.scHighest }]}>
        <ScrollView nestedScrollEnabled style={{ maxHeight: 260 }}>
          {m.consoleLines.length === 0 ? <T variant="mono" color={c.onSurfaceVariant}>No BLE traffic yet.</T> : null}
          {m.consoleLines.slice(-120).map((line, i) => (
            <T key={i} variant="mono" color={line.includes("APP -> ESP") ? c.onSurfaceVariant : c.primary}>
              {line}
            </T>
          ))}
        </ScrollView>
      </View>
      <T variant="labelSmall" color={c.onSurfaceVariant}>Last response: {m.lastAck}</T>
    </Sheet>
  );
}

const styles = StyleSheet.create({
  toolHead: { flexDirection: "row", alignItems: "center", gap: 12 },
  logHead: { flexDirection: "row", alignItems: "center", gap: 4, paddingTop: 4 },
  log: { borderRadius: 16, padding: 12, minHeight: 150 },
});
