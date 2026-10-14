// BLE JSON protocol between the app and the ESP32 firmware (see README.md).

export type ObdCommand =
  | "start_stream"
  | "stop_stream"
  | "read_dtc"
  | "clear_dtc"
  | "read_freeze_frame"
  | "read_readiness"
  | "read_monitor_tests"
  | "vehicle_info"
  | "probe_capabilities"
  | "detect_protocol"
  | "set_link"
  | "bus_diag"
  | "bus_scan"
  | "ext_add"
  | "ext_clear"
  | "ext_list"
  | "set_power"
  | "ota_begin"
  | "ota_end"
  | "ota_abort"
  | "set_pin"
  | "forget_bonds"
  | "device_info";

export type CanBaudKbps = 100 | 125 | 250 | 500 | 1000;
export type CanLinkMode = "listen_only" | "active";
export type StreamMode = "telemetry" | "raw_frames";

/** Sent to ESP in `set_link` as `diagnostic_profile`; selects stack on the MCU. */
export type DiagnosticProfileId = "obd_mode01" | "smart_ed3";

export type LinkConfig = {
  baudKbps: CanBaudKbps;
  mode: CanLinkMode;
  streamMode: StreamMode;
  diagnosticProfile: DiagnosticProfileId;
  /** "auto" makes the ESP detect CAN 11/29-bit at 500/250 kbps. */
  protocol?: string;
};

/** Latest value of every telemetry key, merged across (partial) messages. */
export type TelemetryValues = Record<string, number>;

export type TelemetryMessage = {
  type: "telemetry";
  status: "ok" | "degraded";
  partial?: boolean;
  ts: number;
  [key: string]: unknown;
};

export type StatusMessage = {
  type: "status";
  connected: boolean;
  mode: CanLinkMode;
  signal: number;
  stream_active: boolean;
  link_baud_kbps?: number;
  stream_mode?: StreamMode;
  diagnostic_profile?: string;
  protocol?: string;
  ts: number;
};

export type LinkMessage = {
  type: "link";
  ok: boolean;
  baud_kbps: number;
  mode: CanLinkMode;
  stream_mode: StreamMode;
  diagnostic_profile?: string;
  ts: number;
};

export type ProtocolMessage = {
  type: "protocol";
  ok: boolean;
  protocol: string;
  auto: boolean;
  reason?: string;
  baud_kbps?: number;
  supported_pids?: string;
  vin?: string;
  ts: number;
};

export type ProtocolProbeMessage = {
  type: "protocol_probe";
  stage: string;
  protocol: string;
  ts: number;
};

export type DeviceMessage = {
  type: "device";
  fw: string;
  wake: string;
  sleep_after_s: number;
  mtu: number;
  ext_pids: number;
  ext_pids_max: number;
  /** Firmware 2.1+: BLE pairing with a PIN is required. */
  pairing?: boolean;
  /** Firmware 2.1+: the PIN is still the factory default (123456). */
  pin_default?: boolean;
  bonds?: number;
  /** Firmware 2.1+: OTA images must carry a signature. */
  ota_signed?: boolean;
  ts: number;
};

export type SecurityMessage = {
  type: "security";
  event: "set_pin" | "forget_bonds";
  ok: boolean;
  reason?: string;
  ts: number;
};

export type BusDiagMessage = {
  type: "bus_diag";
  trigger: string;
  twai: Record<string, unknown>;
  ts: number;
};

export type BusScanStartMessage = { type: "bus_scan_start"; count: number; window_ms: number; ts: number };

export type BusScanResultMessage = {
  type: "bus_scan_result";
  index: number;
  baud_kbps: number;
  frames: number;
  bus_errors_delta: number;
  installed: boolean;
  first_id?: string;
  ts: number;
};

export type BusScanDoneMessage = { type: "bus_scan_done"; streaming_was_enabled: boolean; ts: number };

export type DtcMessage = {
  type: "dtc";
  status?: "ok" | "no_response" | "no_link";
  codes: string[];
  pending?: string[];
  permanent?: string[];
  truncated?: boolean;
  ts: number;
};

export type ClearDtcMessage = {
  type: "clear_dtc";
  status: "confirm_required" | "cleared" | "refused" | "failed" | "locked" | "no_link";
  token?: string;
  expires_s?: number;
  reason?: string;
  ecus?: number;
  nrc?: number;
  ts: number;
};

export type FreezeFrameMessage = {
  type: "freeze_frame";
  status: "ok" | "none" | "no_link";
  dtc?: string;
  partial?: boolean;
  ts: number;
  [key: string]: unknown;
};

export type ReadinessMessage = {
  type: "readiness";
  status: "ok" | "no_response" | "no_link";
  mil?: boolean;
  dtc_count?: number;
  ignition?: "spark" | "compression";
  complete?: string[];
  incomplete?: string[];
  ts: number;
};

export type MonitorTestMessage = {
  type: "monitor_test";
  ecu: string;
  mid: string;
  tid: string;
  uas: string;
  pass: boolean;
  value?: number;
  min?: number;
  max?: number;
  unit?: string;
  raw?: number;
  raw_min?: number;
  raw_max?: number;
  ts: number;
};

export type MonitorTestsDoneMessage = {
  type: "monitor_tests_done";
  status: "ok" | "no_link";
  count: number;
  ts: number;
};

export type VehicleInfoMessage = {
  type: "vehicle_info";
  status: "ok" | "no_link";
  vin?: string;
  cal_ids?: string[];
  ecu_names?: string[];
  ts: number;
};

export type ExtAddMessage = {
  type: "ext_add";
  ok: boolean;
  key: string;
  count: number;
  reason?: string;
};

export type ExtListMessage = {
  type: "ext_list";
  count: number;
  max: number;
  part: number;
  last: boolean;
  keys: string[];
  ts: number;
};

export type OtaMessage = {
  type: "ota";
  status: "ready" | "progress" | "done" | "aborted" | "error";
  received: number;
  size: number;
  reason?: string;
  mtu: number;
  ts: number;
};

export type AckMessage = {
  type: "ack";
  command: string;
  status: "ok" | "locked" | "error";
  reason?: string;
  ts: number;
};

export type RawFrameMessage = { type: "raw_frame"; id: number; dlc: number; data: string; ts: number };

export type CapabilityMessage = {
  type: "capability";
  supported_metrics: string[];
  missing_metrics: string[];
  confirmed_from_vehicle: boolean;
  part?: number;
  parts?: number;
  ts: number;
};

export type LogMessage = {
  type: "log";
  level: "info" | "warn" | "error";
  event: string;
  command?: string;
  reason?: string;
  ts: number;
};

export type ObdInboundMessage =
  | TelemetryMessage
  | StatusMessage
  | LinkMessage
  | ProtocolMessage
  | ProtocolProbeMessage
  | DeviceMessage
  | SecurityMessage
  | DtcMessage
  | ClearDtcMessage
  | FreezeFrameMessage
  | ReadinessMessage
  | MonitorTestMessage
  | MonitorTestsDoneMessage
  | VehicleInfoMessage
  | ExtAddMessage
  | ExtListMessage
  | OtaMessage
  | AckMessage
  | RawFrameMessage
  | CapabilityMessage
  | LogMessage
  | BusDiagMessage
  | BusScanStartMessage
  | BusScanResultMessage
  | BusScanDoneMessage;
