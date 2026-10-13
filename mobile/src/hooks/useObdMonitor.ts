import { useCallback, useEffect, useMemo, useRef, useState } from "react";
import { Alert, Share } from "react-native";

import { APP_CONFIG } from "../config";
import { bleObdClient } from "../services/bleObdClient";
import { downloadFirmware, fetchFirmwareInfo, isNewerVersion, type FirmwareInfo } from "../services/firmwareRepository";
import {
  extAddBody,
  firmwareKey,
  type ExtSignalSelection,
} from "../services/profileRepository";
import { parseInboundMessage } from "../services/protocol";
import type {
  BusScanResultMessage,
  DeviceMessage,
  DtcMessage,
  ExtAddMessage,
  MonitorTestMessage,
  ObdCommand,
  OtaMessage,
  ObdInboundMessage,
  ProtocolMessage,
  ReadinessMessage,
  StatusMessage,
  TelemetryValues,
  VehicleInfoMessage,
} from "../types/protocol";
import { VEHICLE_MODES, type VehicleKind, type VehicleMode } from "../types/vehicles";

// The console keeps at most this many lines and is flushed to React state
// in batches, so heavy BLE traffic never re-renders the dashboard per notify.
const MAX_CONSOLE_LINES = 300;
const CONSOLE_FLUSH_INTERVAL_MS = 250;
const EXT_ADD_TIMEOUT_MS = 2000;

function timestampPrefix(): string {
  const d = new Date();
  const pad = (n: number, w = 2) => String(n).padStart(w, "0");
  return `${pad(d.getHours())}:${pad(d.getMinutes())}:${pad(d.getSeconds())}.${pad(d.getMilliseconds(), 3)}`;
}

export type FirmwareState = {
  phase: "idle" | "checking" | "up_to_date" | "available" | "downloading" | "uploading" | "installing" | "done" | "error";
  message: string;
  progress: number; // 0..1 while downloading / uploading
  latest: FirmwareInfo | null;
};

const OTA_READY_TIMEOUT_MS = 8000;
const OTA_DONE_TIMEOUT_MS = 20000;
const RECONNECT_DELAY_MS = 5000;

export type ExtLoadState = { running: boolean; sent: number; total: number; failed: string[] };

export function useObdMonitor() {
  const [connected, setConnected] = useState(false);
  const [connecting, setConnecting] = useState(false);
  const [lastAck, setLastAck] = useState("none");

  const [modeId, setModeId] = useState<VehicleKind>("generic");
  const mode = useMemo<VehicleMode>(() => VEHICLE_MODES.find((m) => m.id === modeId) ?? VEHICLE_MODES[0], [modeId]);
  const [selectedMetrics, setSelectedMetrics] = useState<string[]>([...VEHICLE_MODES[0].defaultMetrics]);

  const [telemetry, setTelemetry] = useState<TelemetryValues>({});
  const [telemetryStatus, setTelemetryStatus] = useState<"ok" | "degraded" | "none">("none");
  const [status, setStatus] = useState<StatusMessage | null>(null);
  const [protocolInfo, setProtocolInfo] = useState<ProtocolMessage | null>(null);
  const [probeStage, setProbeStage] = useState<string | null>(null);
  const [device, setDevice] = useState<DeviceMessage | null>(null);

  const [supportedMetrics, setSupportedMetrics] = useState<string[]>([]);
  const [missingMetrics, setMissingMetrics] = useState<string[]>([]);
  const [capabilitiesConfirmed, setCapabilitiesConfirmed] = useState(false);

  const [dtc, setDtc] = useState<DtcMessage | null>(null);
  const [freezeFrame, setFreezeFrame] = useState<Record<string, unknown> | null>(null);
  const [readiness, setReadiness] = useState<ReadinessMessage | null>(null);
  const [monitorTests, setMonitorTests] = useState<MonitorTestMessage[]>([]);
  const [monitorRunning, setMonitorRunning] = useState(false);
  const [vehicleInfo, setVehicleInfo] = useState<VehicleInfoMessage | null>(null);

  const [extSignals, setExtSignals] = useState<ExtSignalSelection[]>([]);
  const [extLoad, setExtLoad] = useState<ExtLoadState>({ running: false, sent: 0, total: 0, failed: [] });
  const [extOnDevice, setExtOnDevice] = useState<string[]>([]);
  const extWaiter = useRef<((msg: ExtAddMessage | null) => void) | null>(null);
  const extListBuffer = useRef<string[]>([]);
  const capabilityBuffer = useRef<{ supported: string[]; missing: string[] }>({ supported: [], missing: [] });

  const [firmware, setFirmware] = useState<FirmwareState>({ phase: "idle", message: "", progress: 0, latest: null });
  const otaWaiter = useRef<((msg: OtaMessage) => void) | null>(null);

  const [busScanResults, setBusScanResults] = useState<BusScanResultMessage[]>([]);
  const [busScanRunning, setBusScanRunning] = useState(false);

  const [consoleLines, setConsoleLines] = useState<string[]>([]);
  const consoleBufferRef = useRef<string[]>([]);

  const pushConsoleLine = useCallback((line: string) => {
    consoleBufferRef.current.push(`${timestampPrefix()}  ${line}`);
  }, []);

  useEffect(() => {
    const flush = () => {
      if (consoleBufferRef.current.length === 0) return;
      const pending = consoleBufferRef.current;
      consoleBufferRef.current = [];
      setConsoleLines((prev) => {
        const merged = prev.concat(pending);
        return merged.length > MAX_CONSOLE_LINES ? merged.slice(merged.length - MAX_CONSOLE_LINES) : merged;
      });
    };
    const id = setInterval(flush, CONSOLE_FLUSH_INTERVAL_MS);
    return () => clearInterval(id);
  }, []);

  const sendCommand = useCallback(
    async (command: ObdCommand, fields?: Record<string, unknown>): Promise<boolean> => {
      pushConsoleLine(`APP -> ESP ${JSON.stringify({ command, ...(fields ?? {}) })}`);
      try {
        await bleObdClient.sendCommand(command, fields);
        return true;
      } catch {
        setLastAck(`${command}: error (ble_write_failed)`);
        return false;
      }
    },
    [pushConsoleLine]
  );

  const confirmClearDtc = useCallback(
    (token: string, expires: number) => {
      Alert.alert(
        "Clear trouble codes?",
        "This erases stored, pending and freeze-frame data and resets the readiness monitors " +
          `(the car will need a full drive cycle before an inspection). Confirm within ${expires} s.`,
        [
          { text: "Cancel", style: "cancel" },
          { text: "Clear codes", style: "destructive", onPress: () => void sendCommand("clear_dtc", { confirm: token }) },
        ]
      );
    },
    [sendCommand]
  );

  const routeMessage = useCallback(
    (msg: ObdInboundMessage) => {
      switch (msg.type) {
        case "telemetry": {
          setTelemetryStatus(msg.status);
          const values: TelemetryValues = {};
          for (const [key, value] of Object.entries(msg)) {
            if (typeof value === "number" && Number.isFinite(value) && key !== "ts") values[key] = value;
          }
          if (Object.keys(values).length > 0) setTelemetry((prev) => ({ ...prev, ...values }));
          return;
        }
        case "status":
          setStatus(msg);
          return;
        case "link":
          setLastAck(`set_link: ${msg.ok ? "ok" : "error"} (${msg.baud_kbps} kbps · ${msg.mode})`);
          return;
        case "protocol_probe":
          setProbeStage(`${msg.stage} ${msg.protocol}`);
          return;
        case "protocol":
          setProbeStage(null);
          setProtocolInfo(msg);
          setLastAck(msg.ok ? `protocol: ${msg.protocol}` : `protocol: not found (${msg.reason ?? "?"})`);
          return;
        case "device":
          setDevice(msg);
          return;
        case "capability": {
          const part = msg.part ?? 1;
          const parts = msg.parts ?? 1;
          if (part === 1) capabilityBuffer.current = { supported: [], missing: [] };
          capabilityBuffer.current.supported.push(...msg.supported_metrics);
          capabilityBuffer.current.missing.push(...msg.missing_metrics);
          if (part === parts) {
            setSupportedMetrics(capabilityBuffer.current.supported);
            setMissingMetrics(capabilityBuffer.current.missing);
            setCapabilitiesConfirmed(msg.confirmed_from_vehicle);
          }
          return;
        }
        case "dtc":
          setDtc(msg);
          return;
        case "clear_dtc":
          if (msg.status === "confirm_required" && msg.token) {
            confirmClearDtc(msg.token, msg.expires_s ?? 30);
          } else if (msg.status === "cleared") {
            setDtc(null);
            setFreezeFrame(null);
            Alert.alert("Codes cleared", `${msg.ecus ?? 1} control unit(s) confirmed.`);
          } else {
            Alert.alert("Codes not cleared", msg.reason === "vehicle_must_be_stopped_engine_off"
              ? "Stop the vehicle and turn the engine off (ignition on), then try again."
              : `Status: ${msg.status}${msg.nrc ? ` (NRC 0x${msg.nrc.toString(16)})` : ""}`);
          }
          return;
        case "freeze_frame": {
          const { type: _t, partial: _p, ts: _ts, ...rest } = msg;
          setFreezeFrame((prev) => (msg.partial || (prev && prev.dtc === rest.dtc) ? { ...(prev ?? {}), ...rest } : rest));
          return;
        }
        case "readiness":
          setReadiness(msg);
          return;
        case "monitor_test":
          setMonitorTests((prev) => [...prev, msg]);
          return;
        case "monitor_tests_done":
          setMonitorRunning(false);
          setLastAck(`monitor tests: ${msg.count}`);
          return;
        case "vehicle_info":
          setVehicleInfo(msg);
          return;
        case "ext_add":
          extWaiter.current?.(msg);
          return;
        case "ota":
          otaWaiter.current?.(msg);
          return;
        case "ext_list":
          if (msg.part === 1) extListBuffer.current = [];
          extListBuffer.current.push(...msg.keys);
          if (msg.last) setExtOnDevice(extListBuffer.current);
          return;
        case "ack": {
          const reason = msg.reason ? ` (${msg.reason})` : "";
          setLastAck(`${msg.command}: ${msg.status}${reason}`);
          return;
        }
        case "log":
          if (msg.level !== "info") setLastAck(`${msg.event}${msg.reason ? ` (${msg.reason})` : ""}`);
          return;
        case "bus_scan_start":
          setBusScanResults([]);
          setBusScanRunning(true);
          return;
        case "bus_scan_result":
          setBusScanResults((prev) => {
            const next = [...prev];
            next[msg.index] = msg;
            return next;
          });
          return;
        case "bus_scan_done":
          setBusScanRunning(false);
          return;
        default:
          return;
      }
    },
    [confirmClearDtc]
  );

  const connect = useCallback(async () => {
    if (connecting) return;
    setConnecting(true);
    try {
      await bleObdClient.connect(
        (payload) => {
          pushConsoleLine(`ESP -> APP ${payload}`);
          const msg = parseInboundMessage(payload);
          if (msg) routeMessage(msg);
          else pushConsoleLine("APP parse warning: malformed JSON payload");
        },
        setConnected
      );
    } catch {
      setLastAck("connection: error (ble_connect_failed)");
    } finally {
      setConnecting(false);
    }
  }, [connecting, pushConsoleLine, routeMessage]);

  useEffect(() => {
    void connect();
    return () => bleObdClient.disconnect();
    // eslint-disable-next-line react-hooks/exhaustive-deps
  }, []);

  // Reconnect automatically after the link drops (out of range, ESP restart after an update).
  useEffect(() => {
    if (connected || connecting) return;
    const id = setTimeout(() => void connect(), RECONNECT_DELAY_MS);
    return () => clearTimeout(id);
  }, [connected, connecting, connect]);

  // Push the link configuration whenever the connection or the vehicle mode changes.
  useEffect(() => {
    if (!connected) return;
    const cfg = mode.link;
    setTelemetry({});
    setProtocolInfo(null);
    void (async () => {
      await sendCommand("stop_stream");
      const fields: Record<string, unknown> = {
        baud_kbps: cfg.baudKbps,
        mode: cfg.mode,
        stream_mode: cfg.streamMode,
        diagnostic_profile: cfg.diagnosticProfile,
      };
      if (cfg.protocol) fields.protocol = cfg.protocol;
      await sendCommand("set_link", fields);
      await sendCommand("ext_list");
    })();
  }, [connected, mode, sendCommand]);

  const selectMode = useCallback((id: VehicleKind) => {
    setModeId(id);
    const next = VEHICLE_MODES.find((m) => m.id === id);
    if (next) setSelectedMetrics([...next.defaultMetrics]);
  }, []);

  const toggleMetric = useCallback((id: string) => {
    setSelectedMetrics((prev) => (prev.includes(id) ? prev.filter((m) => m !== id) : [...prev, id]));
  }, []);

  const startStream = useCallback(() => {
    setTelemetry({});
    // Smart ED streams everything; the selection only filters what is shown.
    const fields = mode.id === "smart_ed3" || selectedMetrics.length === 0 ? undefined : { metrics: selectedMetrics };
    void sendCommand("start_stream", fields);
  }, [mode.id, selectedMetrics, sendCommand]);

  const stopStream = useCallback(() => void sendCommand("stop_stream"), [sendCommand]);

  const readMonitorTests = useCallback(() => {
    setMonitorTests([]);
    setMonitorRunning(true);
    void sendCommand("read_monitor_tests");
  }, [sendCommand]);

  const readFreezeFrame = useCallback(() => {
    setFreezeFrame(null);
    void sendCommand("read_freeze_frame");
  }, [sendCommand]);

  const waitForExtAdd = () =>
    new Promise<ExtAddMessage | null>((resolve) => {
      const timer = setTimeout(() => {
        extWaiter.current = null;
        resolve(null);
      }, EXT_ADD_TIMEOUT_MS);
      extWaiter.current = (msg) => {
        clearTimeout(timer);
        extWaiter.current = null;
        resolve(msg);
      };
    });

  /** Replaces the manufacturer signals on the ESP with `selections`. */
  const loadExtSignals = useCallback(
    async (selections: ExtSignalSelection[]) => {
      const list = selections.slice(0, APP_CONFIG.maxExtSignals);
      setExtLoad({ running: true, sent: 0, total: list.length, failed: [] });
      const failed: string[] = [];
      await sendCommand("stop_stream");
      await sendCommand("ext_clear");
      for (let i = 0; i < list.length; i++) {
        const reply = waitForExtAdd();
        const sent = await sendCommand("ext_add", extAddBody(list[i]));
        const result = sent ? await reply : null;
        if (!result || !result.ok) failed.push(list[i].key);
        setExtLoad({ running: true, sent: i + 1, total: list.length, failed: [...failed] });
      }
      setExtSignals(list.filter((s) => !failed.includes(s.key)));
      setExtLoad({ running: false, sent: list.length, total: list.length, failed });
      await sendCommand("ext_list");
    },
    [sendCommand]
  );

  const clearExtSignals = useCallback(async () => {
    setExtSignals([]);
    await sendCommand("ext_clear");
  }, [sendCommand]);

  const waitForOta = (accept: OtaMessage["status"][], timeoutMs: number) =>
    new Promise<OtaMessage | null>((resolve) => {
      const timer = setTimeout(() => {
        otaWaiter.current = null;
        resolve(null);
      }, timeoutMs);
      otaWaiter.current = (msg) => {
        if (msg.status === "error" || msg.status === "aborted" || accept.includes(msg.status)) {
          clearTimeout(timer);
          otaWaiter.current = null;
          resolve(msg);
        }
      };
    });

  const checkFirmware = useCallback(async () => {
    setFirmware({ phase: "checking", message: "Checking GitHub…", progress: 0, latest: null });
    try {
      const info = await fetchFirmwareInfo();
      const newer = isNewerVersion(info.version, device?.fw);
      setFirmware({
        phase: newer ? "available" : "up_to_date",
        message: newer
          ? `Version ${info.version} is available (installed: ${device?.fw ?? "unknown"}).`
          : `Firmware ${device?.fw ?? info.version} is up to date.`,
        progress: 0,
        latest: info,
      });
    } catch (e) {
      setFirmware({ phase: "error", message: (e as Error).message, progress: 0, latest: null });
    }
  }, [device]);

  /** Downloads the firmware and writes it to the ESP over BLE; the ESP verifies the MD5 and restarts. */
  const installFirmware = useCallback(async () => {
    const info = firmware.latest;
    if (!info || !connected) return;
    const fail = async (message: string, abort: boolean) => {
      if (abort) await sendCommand("ota_abort");
      setFirmware({ phase: "error", message, progress: 0, latest: info });
    };
    try {
      setFirmware({ phase: "downloading", message: "Downloading firmware…", progress: 0, latest: info });
      const image = await downloadFirmware(info);

      await sendCommand("stop_stream");
      const ready = waitForOta(["ready"], OTA_READY_TIMEOUT_MS);
      await sendCommand("ota_begin", { size: image.length, md5: info.md5 });
      const begin = await ready;
      if (!begin || begin.status !== "ready") {
        await fail(`ESP refused the update (${begin?.reason ?? "no answer"})`, false);
        return;
      }

      const chunk = Math.max(20, Math.min(240, bleObdClient.mtu() - 3));
      for (let offset = 0, n = 0; offset < image.length; offset += chunk, n++) {
        await bleObdClient.writeOtaChunk(image.subarray(offset, Math.min(offset + chunk, image.length)));
        if (n % 8 === 7) await new Promise((r) => setTimeout(r, 15)); // let the ESP drain its BLE queue
        if (n % 20 === 0) {
          setFirmware({ phase: "uploading", message: "Uploading to ESP…", progress: offset / image.length, latest: info });
        }
      }

      setFirmware({ phase: "installing", message: "Verifying and installing…", progress: 1, latest: info });
      const done = waitForOta(["done"], OTA_DONE_TIMEOUT_MS);
      await sendCommand("ota_end");
      const result = await done;
      if (result?.status === "done") {
        setFirmware({
          phase: "done",
          message: `Version ${info.version} installed. The ESP is restarting; the app reconnects by itself.`,
          progress: 1,
          latest: info,
        });
      } else {
        await fail(`Update failed (${result?.reason ?? "no answer"}). The old firmware stays active.`, true);
      }
    } catch (e) {
      await fail((e as Error).message, true);
    }
  }, [connected, firmware.latest, sendCommand]);

  const setSleepAfter = useCallback(
    (seconds: number) => void sendCommand("set_power", { sleep_after_s: seconds }),
    [sendCommand]
  );

  const exportConsoleLog = useCallback(async () => {
    const header = [
      "OBD BLE Monitor log",
      `exported_at: ${new Date().toISOString()}`,
      `vehicle_mode: ${mode.label}`,
      `protocol: ${protocolInfo?.protocol ?? status?.protocol ?? "unknown"}`,
      `firmware: ${device?.fw ?? "unknown"}`,
      `selected_metrics: ${selectedMetrics.join(",") || "(none)"}`,
      `ext_signals: ${extSignals.map((s) => s.key).join(",") || "(none)"}`,
      `last_ack: ${lastAck}`,
      "----- BEGIN CONSOLE -----",
    ].join("\n");
    try {
      await Share.share({ message: `${header}\n${consoleLines.join("\n")}\n----- END CONSOLE -----`, title: "OBD BLE log" });
    } catch {
      // user dismissed the share sheet
    }
  }, [consoleLines, device, extSignals, lastAck, mode.label, protocolInfo, selectedMetrics, status]);

  /** Telemetry keys of the loaded manufacturer signals, as the firmware sends them. */
  const extValues = useMemo(
    () =>
      extSignals.map((s) => ({
        key: s.key,
        name: s.name,
        unit: s.unit,
        map: s.signal.map,
        value: telemetry[firmwareKey(s.key)],
      })),
    [extSignals, telemetry]
  );

  return {
    connected,
    connecting,
    connect,
    lastAck,
    modes: VEHICLE_MODES,
    mode,
    selectMode,
    selectedMetrics,
    setSelectedMetrics,
    toggleMetric,
    telemetry,
    telemetryStatus,
    status,
    protocolInfo,
    probeStage,
    device,
    supportedMetrics,
    missingMetrics,
    capabilitiesConfirmed,
    dtc,
    freezeFrame,
    readiness,
    monitorTests,
    monitorRunning,
    vehicleInfo,
    extSignals,
    extValues,
    extLoad,
    extOnDevice,
    firmware,
    checkFirmware,
    installFirmware,
    loadExtSignals,
    clearExtSignals,
    busScanResults,
    busScanRunning,
    consoleLines,
    clearConsole: () => {
      consoleBufferRef.current = [];
      setConsoleLines([]);
    },
    exportConsoleLog,
    sendCommand,
    startStream,
    stopStream,
    readMonitorTests,
    readFreezeFrame,
    setSleepAfter,
  };
}

export type ObdMonitor = ReturnType<typeof useObdMonitor>;
