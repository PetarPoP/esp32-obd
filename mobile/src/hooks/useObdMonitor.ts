import { useCallback, useEffect, useRef, useState } from "react";
import { Share } from "react-native";

import { APP_CONFIG } from "../config";
import { bleObdClient } from "../services/bleObdClient";
import { downloadFirmware, fetchFirmwareInfo, isNewerVersion, type FirmwareInfo } from "../services/firmwareRepository";
import { extAddBody, firmwareKey } from "../services/profileRepository";
import { parseInboundMessage } from "../services/protocol";
import type {
  BusScanResultMessage,
  DeviceMessage,
  DtcMessage,
  ExtAddMessage,
  MonitorTestMessage,
  ObdCommand,
  ObdInboundMessage,
  OtaMessage,
  ProtocolMessage,
  ReadinessMessage,
  StatusMessage,
  TelemetryValues,
  VehicleInfoMessage,
} from "../types/protocol";
import { linkConfig, vehicleName, type StoredExtSignal, type VehicleSelection, type VehicleValues } from "../types/vehicles";

// The console keeps at most this many lines and is flushed to React state
// in batches, so heavy BLE traffic never re-renders the dashboard per notify.
const MAX_CONSOLE_LINES = 300;
const CONSOLE_FLUSH_INTERVAL_MS = 250;
const EXT_ADD_TIMEOUT_MS = 2000;
const OTA_READY_TIMEOUT_MS = 8000;
const OTA_DONE_TIMEOUT_MS = 20000;
const RECONNECT_DELAY_MS = 5000;
const RSSI_INTERVAL_MS = 5000;
// Pairing: the user may need a while to type the PIN.
const HANDSHAKE_ATTEMPTS = 25;
const HANDSHAKE_WAIT_MS = 3000;
/** Charts keep this much history per value. */
export const HISTORY_WINDOW_MS = 30000;
const TOAST_MS = 3500;

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

export type ExtLoadState = { running: boolean; sent: number; total: number; failed: string[] };
export type HistoryPoint = { t: number; v: number };
export type LinkState = "disconnected" | "connecting" | "pairing" | "ready";

type Options = { vehicle: VehicleSelection; values: VehicleValues };

export function useObdMonitor({ vehicle, values }: Options) {
  const [link, setLink] = useState<LinkState>("disconnected");
  const [bleConnected, setBleConnected] = useState(false);
  const [connecting, setConnecting] = useState(false);
  const [rssi, setRssi] = useState<number | null>(null);
  const [lastAck, setLastAck] = useState("none");
  const [toast, setToast] = useState<string | null>(null);

  const [telemetry, setTelemetry] = useState<TelemetryValues>({});
  const [telemetryStatus, setTelemetryStatus] = useState<"ok" | "degraded" | "none">("none");
  const history = useRef<Map<string, HistoryPoint[]>>(new Map());
  const [status, setStatus] = useState<StatusMessage | null>(null);
  const [protocolInfo, setProtocolInfo] = useState<ProtocolMessage | null>(null);
  const [probeStage, setProbeStage] = useState<string | null>(null);
  const [device, setDevice] = useState<DeviceMessage | null>(null);

  const [supportedMetrics, setSupportedMetrics] = useState<string[]>([]);
  const [capabilitiesConfirmed, setCapabilitiesConfirmed] = useState(false);
  const capabilityBuffer = useRef<string[]>([]);

  const [dtc, setDtc] = useState<DtcMessage | null>(null);
  const [dtcReading, setDtcReading] = useState(false);
  const [clearConfirm, setClearConfirm] = useState<{ token: string; expires: number } | null>(null);
  const [freezeFrame, setFreezeFrame] = useState<Record<string, unknown> | null>(null);
  const [readiness, setReadiness] = useState<ReadinessMessage | null>(null);
  const [monitorTests, setMonitorTests] = useState<MonitorTestMessage[]>([]);
  const [monitorRunning, setMonitorRunning] = useState(false);
  const [vehicleInfo, setVehicleInfo] = useState<VehicleInfoMessage | null>(null);

  const [extLoad, setExtLoad] = useState<ExtLoadState>({ running: false, sent: 0, total: 0, failed: [] });
  const [extOnDevice, setExtOnDevice] = useState<string[]>([]);
  const extWaiter = useRef<((msg: ExtAddMessage | null) => void) | null>(null);
  const extListBuffer = useRef<string[]>([]);
  const extSyncPending = useRef(false);

  const [firmware, setFirmware] = useState<FirmwareState>({ phase: "idle", message: "", progress: 0, latest: null });
  const otaWaiter = useRef<((msg: OtaMessage) => void) | null>(null);

  const [busScanResults, setBusScanResults] = useState<BusScanResultMessage[]>([]);
  const [busScanRunning, setBusScanRunning] = useState(false);
  const [busDiag, setBusDiag] = useState<string | null>(null);

  const [consoleLines, setConsoleLines] = useState<string[]>([]);
  const consoleBufferRef = useRef<string[]>([]);
  const inboundSeen = useRef(false);

  // Latest options for callbacks that run from BLE events.
  const vehicleRef = useRef(vehicle);
  const valuesRef = useRef(values);
  vehicleRef.current = vehicle;
  valuesRef.current = values;

  const showToast = useCallback((text: string) => setToast(text), []);
  useEffect(() => {
    if (!toast) return;
    const id = setTimeout(() => setToast(null), TOAST_MS);
    return () => clearTimeout(id);
  }, [toast]);

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
      const shown = command === "set_pin" ? { ...fields, pin: "******" } : fields;
      pushConsoleLine(`APP -> ESP ${JSON.stringify({ command, ...(shown ?? {}) })}`);
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

  /** Replaces the manufacturer signals on the ESP with `list`. */
  const loadExtSignals = useCallback(
    async (list: StoredExtSignal[]) => {
      const signals = list.slice(0, APP_CONFIG.maxExtSignals);
      setExtLoad({ running: true, sent: 0, total: signals.length, failed: [] });
      const failed: string[] = [];
      await sendCommand("stop_stream");
      await sendCommand("ext_clear");
      for (let i = 0; i < signals.length; i++) {
        const reply = waitForExtAdd();
        const sent = await sendCommand("ext_add", extAddBody(signals[i]));
        const result = sent ? await reply : null;
        if (!result || !result.ok) failed.push(signals[i].key);
        setExtLoad({ running: true, sent: i + 1, total: signals.length, failed: [...failed] });
      }
      setExtLoad({ running: false, sent: signals.length, total: signals.length, failed });
      if (failed.length > 0) showToast(`${failed.length} manufacturer value(s) were rejected by the adapter`);
      await sendCommand("ext_list");
    },
    [sendCommand, showToast]
  );

  /** Manufacturer signals the ESP should hold for the current vehicle. */
  const desiredExt = () => (vehicleRef.current.kind === "profile" ? valuesRef.current.ext : []);

  const routeMessage = useCallback(
    (msg: ObdInboundMessage) => {
      inboundSeen.current = true;
      switch (msg.type) {
        case "telemetry": {
          setTelemetryStatus(msg.status);
          const fresh: TelemetryValues = {};
          const now = Date.now();
          for (const [key, value] of Object.entries(msg)) {
            if (typeof value !== "number" || !Number.isFinite(value) || key === "ts") continue;
            fresh[key] = value;
            const series = history.current.get(key) ?? [];
            series.push({ t: now, v: value });
            while (series.length > 0 && now - series[0].t > HISTORY_WINDOW_MS) series.shift();
            history.current.set(key, series);
          }
          if (Object.keys(fresh).length > 0) setTelemetry((prev) => ({ ...prev, ...fresh }));
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
        case "security":
          if (msg.event === "set_pin") {
            showToast(msg.ok ? "PIN changed. New phones pair with the new PIN." : `PIN not changed (${msg.reason ?? "error"})`);
          } else {
            showToast("Paired phones forgotten. Remove OBD-ESP32 in Android Bluetooth settings, then pair again.");
          }
          return;
        case "capability": {
          const part = msg.part ?? 1;
          const parts = msg.parts ?? 1;
          if (part === 1) capabilityBuffer.current = [];
          capabilityBuffer.current.push(...msg.supported_metrics);
          if (part === parts) {
            setSupportedMetrics(capabilityBuffer.current);
            setCapabilitiesConfirmed(msg.confirmed_from_vehicle);
          }
          return;
        }
        case "dtc":
          setDtc(msg);
          setDtcReading(false);
          return;
        case "clear_dtc":
          if (msg.status === "confirm_required" && msg.token) {
            setClearConfirm({ token: msg.token, expires: msg.expires_s ?? 30 });
          } else if (msg.status === "cleared") {
            setDtc({ type: "dtc", status: "ok", codes: [], pending: [], permanent: [], ts: msg.ts });
            setFreezeFrame(null);
            showToast(`Codes cleared (${msg.ecus ?? 1} control unit${(msg.ecus ?? 1) === 1 ? "" : "s"})`);
          } else if (msg.reason === "vehicle_must_be_stopped_engine_off") {
            showToast("Not cleared: stop the car, engine off, ignition on.");
          } else {
            showToast(`Codes not cleared (${msg.status}${msg.nrc ? `, NRC 0x${msg.nrc.toString(16)}` : ""})`);
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
          if (msg.last) {
            const onDevice = extListBuffer.current;
            setExtOnDevice(onDevice);
            if (extSyncPending.current) {
              // Bring the ESP in line with the values chosen for this vehicle.
              extSyncPending.current = false;
              const wanted = desiredExt();
              const want = new Set(wanted.map((s) => firmwareKey(s.key)));
              const same = want.size === onDevice.length && onDevice.every((k) => want.has(k));
              if (!same) void loadExtSignals(wanted);
            }
          }
          return;
        case "ack": {
          const reason = msg.reason ? ` (${msg.reason})` : "";
          setLastAck(`${msg.command}: ${msg.status}${reason}`);
          return;
        }
        case "log":
          if (msg.level !== "info") setLastAck(`${msg.event}${msg.reason ? ` (${msg.reason})` : ""}`);
          return;
        case "bus_diag":
          setBusDiag(JSON.stringify(msg.twai));
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
    // eslint-disable-next-line react-hooks/exhaustive-deps
    [loadExtSignals, showToast]
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
        setBleConnected
      );
    } catch {
      setLastAck("connection: error (ble_connect_failed)");
    } finally {
      setConnecting(false);
    }
  }, [connecting, pushConsoleLine, routeMessage]);

  const disconnect = useCallback(() => {
    bleObdClient.disconnect();
    setBleConnected(false);
  }, []);

  useEffect(() => {
    void connect();
    return () => bleObdClient.disconnect();
    // eslint-disable-next-line react-hooks/exhaustive-deps
  }, []);

  // Reconnect automatically after the link drops (out of range, ESP restart after an update).
  useEffect(() => {
    if (bleConnected || connecting) return;
    const id = setTimeout(() => void connect(), RECONNECT_DELAY_MS);
    return () => clearTimeout(id);
  }, [bleConnected, connecting, connect]);

  // After connecting, wait until the ESP answers. With firmware 2.1+ nothing
  // comes back before the phone is paired, so keep asking (and re-subscribe)
  // while the user enters the PIN.
  useEffect(() => {
    if (!bleConnected) {
      setLink(connecting ? "connecting" : "disconnected");
      setRssi(null);
      return;
    }
    let cancelled = false;
    setLink("pairing");
    inboundSeen.current = false;
    void (async () => {
      for (let attempt = 0; attempt < HANDSHAKE_ATTEMPTS && !cancelled; attempt++) {
        if (attempt > 0) bleObdClient.resubscribe();
        await sendCommand("device_info");
        const deadline = Date.now() + HANDSHAKE_WAIT_MS;
        while (!cancelled && !inboundSeen.current && Date.now() < deadline) {
          await new Promise((r) => setTimeout(r, 150));
        }
        if (inboundSeen.current) {
          if (!cancelled) setLink("ready");
          return;
        }
      }
      if (!cancelled) {
        showToast("The adapter does not answer. Check the PIN and try again.");
        disconnect();
      }
    })();
    return () => {
      cancelled = true;
    };
  }, [bleConnected, connecting, disconnect, sendCommand, showToast]);

  const ready = link === "ready";

  // Signal strength for the link card.
  useEffect(() => {
    if (!ready) return;
    const poll = () => void bleObdClient.readRssi().then(setRssi);
    poll();
    const id = setInterval(poll, RSSI_INTERVAL_MS);
    return () => clearInterval(id);
  }, [ready]);

  // Push the link configuration whenever the connection or the vehicle changes.
  const linkKey = vehicle.kind === "profile" ? `profile:${vehicle.file}` : vehicle.kind;
  useEffect(() => {
    if (!ready) return;
    const cfg = linkConfig(vehicleRef.current);
    setTelemetry({});
    history.current.clear();
    setProtocolInfo(null);
    setSupportedMetrics([]);
    setCapabilitiesConfirmed(false);
    setDtc(null);
    extSyncPending.current = true;
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
  }, [ready, linkKey, sendCommand]);

  const streaming = ready && (status?.stream_active ?? false);

  const startStream = useCallback(
    (override?: VehicleValues) => {
      const v = override ?? valuesRef.current;
      setTelemetry({});
      history.current.clear();
      // Smart ED streams everything; the selection only filters what is shown.
      const fields = vehicleRef.current.kind === "smart_ed3" || v.metrics.length === 0 ? undefined : { metrics: v.metrics };
      void sendCommand("start_stream", fields);
    },
    [sendCommand]
  );

  const stopStream = useCallback(() => void sendCommand("stop_stream"), [sendCommand]);

  /** Applies a new value selection: loads manufacturer signals if needed and restarts the stream. */
  const applyValues = useCallback(
    async (next: VehicleValues) => {
      if (!ready) return;
      const wasStreaming = status?.stream_active ?? false;
      if (vehicleRef.current.kind === "profile") {
        const want = next.ext.map((s) => firmwareKey(s.key));
        const same = want.length === extOnDevice.length && want.every((k) => extOnDevice.includes(k));
        if (!same) await loadExtSignals(next.ext);
      }
      if (wasStreaming) startStream(next);
    },
    [extOnDevice, loadExtSignals, ready, startStream, status]
  );

  const readDtc = useCallback(() => {
    setDtcReading(true);
    void sendCommand("read_dtc");
  }, [sendCommand]);

  const requestClearDtc = useCallback(() => void sendCommand("clear_dtc"), [sendCommand]);
  const confirmClearDtc = useCallback(() => {
    if (clearConfirm) void sendCommand("clear_dtc", { confirm: clearConfirm.token });
    setClearConfirm(null);
  }, [clearConfirm, sendCommand]);
  const cancelClearDtc = useCallback(() => setClearConfirm(null), []);

  const readMonitorTests = useCallback(() => {
    setMonitorTests([]);
    setMonitorRunning(true);
    void sendCommand("read_monitor_tests");
  }, [sendCommand]);

  const readFreezeFrame = useCallback(() => {
    setFreezeFrame(null);
    void sendCommand("read_freeze_frame");
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

  /** Downloads the firmware and writes it to the ESP over BLE; the ESP checks signature and MD5, then restarts. */
  const installFirmware = useCallback(async () => {
    const info = firmware.latest;
    if (!info || !ready) return;
    const fail = async (message: string, abort: boolean) => {
      if (abort) await sendCommand("ota_abort");
      setFirmware({ phase: "error", message, progress: 0, latest: info });
    };
    if (device?.ota_signed && !info.sig) {
      await fail("This image is not signed; the adapter only accepts signed firmware.", false);
      return;
    }
    try {
      setFirmware({ phase: "downloading", message: "Downloading firmware…", progress: 0, latest: info });
      const image = await downloadFirmware(info);

      await sendCommand("stop_stream");
      const readyMsg = waitForOta(["ready"], OTA_READY_TIMEOUT_MS);
      const begin: Record<string, unknown> = { size: image.length, md5: info.md5 };
      if (info.sig) begin.sig = info.sig;
      await sendCommand("ota_begin", begin);
      const started = await readyMsg;
      if (!started || started.status !== "ready") {
        await fail(`The adapter refused the update (${started?.reason ?? "no answer"})`, false);
        return;
      }

      const chunk = Math.max(20, Math.min(240, bleObdClient.mtu() - 3));
      for (let offset = 0, n = 0; offset < image.length; offset += chunk, n++) {
        await bleObdClient.writeOtaChunk(image.subarray(offset, Math.min(offset + chunk, image.length)));
        if (n % 8 === 7) await new Promise((r) => setTimeout(r, 15)); // let the ESP drain its BLE queue
        if (n % 20 === 0) {
          setFirmware({ phase: "uploading", message: "Sending to the adapter…", progress: offset / image.length, latest: info });
        }
      }

      setFirmware({ phase: "installing", message: "Checking signature and installing…", progress: 1, latest: info });
      const done = waitForOta(["done"], OTA_DONE_TIMEOUT_MS);
      await sendCommand("ota_end");
      const result = await done;
      if (result?.status === "done") {
        setFirmware({
          phase: "done",
          message: `Version ${info.version} installed. The adapter restarts and the app reconnects by itself.`,
          progress: 1,
          latest: info,
        });
      } else {
        const why = result?.reason === "bad_signature" ? "signature does not match" : result?.reason ?? "no answer";
        await fail(`Update failed (${why}). The old firmware stays active.`, true);
      }
    } catch (e) {
      await fail((e as Error).message, true);
    }
  }, [device, firmware.latest, ready, sendCommand]);

  const setSleepAfter = useCallback((seconds: number) => void sendCommand("set_power", { sleep_after_s: seconds }), [sendCommand]);
  const setPin = useCallback((pin: string) => void sendCommand("set_pin", { pin }), [sendCommand]);
  const forgetBonds = useCallback(() => void sendCommand("forget_bonds"), [sendCommand]);

  const exportConsoleLog = useCallback(async () => {
    const header = [
      "OBD BLE Monitor log",
      `exported_at: ${new Date().toISOString()}`,
      `vehicle: ${vehicleName(vehicle)}`,
      `protocol: ${protocolInfo?.protocol ?? status?.protocol ?? "unknown"}`,
      `firmware: ${device?.fw ?? "unknown"}`,
      `metrics: ${values.metrics.join(",") || "(none)"}`,
      `ext_signals: ${values.ext.map((s) => s.key).join(",") || "(none)"}`,
      `last_ack: ${lastAck}`,
      "----- BEGIN CONSOLE -----",
    ].join("\n");
    try {
      await Share.share({ message: `${header}\n${consoleLines.join("\n")}\n----- END CONSOLE -----`, title: "OBD BLE log" });
    } catch {
      // user dismissed the share sheet
    }
  }, [consoleLines, device, lastAck, protocolInfo, status, values, vehicle]);

  return {
    link,
    ready,
    connect,
    disconnect,
    rssi,
    lastAck,
    toast,
    showToast,
    telemetry,
    telemetryStatus,
    history: history.current,
    status,
    streaming,
    protocolInfo,
    probeStage,
    device,
    supportedMetrics,
    capabilitiesConfirmed,
    dtc,
    dtcReading,
    readDtc,
    clearConfirm,
    requestClearDtc,
    confirmClearDtc,
    cancelClearDtc,
    freezeFrame,
    readFreezeFrame,
    readiness,
    monitorTests,
    monitorRunning,
    readMonitorTests,
    vehicleInfo,
    extLoad,
    extOnDevice,
    applyValues,
    firmware,
    checkFirmware,
    installFirmware,
    busScanResults,
    busScanRunning,
    busDiag,
    consoleLines,
    clearConsole: () => {
      consoleBufferRef.current = [];
      setConsoleLines([]);
    },
    exportConsoleLog,
    sendCommand,
    startStream,
    stopStream,
    setSleepAfter,
    setPin,
    forgetBonds,
  };
}

export type ObdMonitor = ReturnType<typeof useObdMonitor>;
