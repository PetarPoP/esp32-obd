#include <Arduino.h>
#include <driver/twai.h>

#include "transport_ble.h"
#include "can_phy_driver.h"
#include "twai_driver.h"
#include "dtc_decoder.h"
#include "obd_transport.h"
#include "pid_decoder.h"
#include "telemetry_field_mask.h"
#include "safety_guard.h"
#include "connection_manager.h"
#include "diagnostics_service.h"
#include "obd_scheduler_service.h"
#include "raw_frame_stream_service.h"
#include "smart_bms_service.h"
#include "telemetry_service.h"

namespace {

int FindJsonValueStartLocal(const String& payload, const char* key) {
  int key_pos = payload.indexOf(key);
  if (key_pos < 0) return -1;
  int colon = payload.indexOf(':', key_pos);
  if (colon < 0) return -1;
  int cursor = colon + 1;
  while (cursor < (int)payload.length() &&
         (payload.charAt(cursor) == ' ' || payload.charAt(cursor) == '\t')) {
    ++cursor;
  }
  return cursor;
}

uint32_t ParseUintFieldLocal(const String& payload, const char* key, uint32_t fallback) {
  const int start = FindJsonValueStartLocal(payload, key);
  if (start < 0) return fallback;
  int end = start;
  while (end < (int)payload.length() && isDigit(payload.charAt(end))) ++end;
  if (end == start) return fallback;
  return (uint32_t)payload.substring(start, end).toInt();
}

String ParseStringFieldLocal(const String& payload, const char* key) {
  const int start = FindJsonValueStartLocal(payload, key);
  if (start < 0) return String();
  if (payload.charAt(start) != '"') return String();
  const int quote_end = payload.indexOf('"', start + 1);
  if (quote_end < 0) return String();
  return payload.substring(start + 1, quote_end);
}

obd::safety::SafetyGuard g_safety_guard;
obd::drivers::CanPhyDriver g_can_phy;
obd::drivers::TwaiDriver g_twai;
obd::core::ObdTransport g_transport(g_twai);
obd::core::PidDecoder g_pid_decoder;
obd::core::DtcDecoder g_dtc_decoder;
obd::app_link::TransportBle g_ble;
obd::services::TelemetryService g_telemetry(g_transport, g_pid_decoder, g_ble);
obd::services::RawFrameStreamService g_raw_frames(g_ble);
obd::services::SmartBmsService g_smart_bms(g_ble, g_twai);
obd::services::DiagnosticsService g_diagnostics(g_transport, g_dtc_decoder, g_pid_decoder, g_ble,
                                                g_safety_guard);
obd::services::ConnectionManager g_connection_manager;
obd::services::ObdSchedulerService g_scheduler(g_transport, g_telemetry, g_raw_frames, g_smart_bms);

// BLE writes arrive on the Bluetooth task. Commands are queued and executed
// from loop() so long operations (protocol detection, DTC reads, bus scans)
// never block the BLE stack or race with the scheduler over the CAN driver.
struct PendingCommand {
  char command[24];
  char payload[240];
};
QueueHandle_t g_command_queue = nullptr;

// True when the protocol came from auto-detection, so a lost link may be
// re-detected automatically.
bool g_auto_protocol = false;
uint32_t g_last_link_lost_ms = 0;
constexpr uint16_t kLinkLostFailures = 12;
constexpr uint32_t kLinkRecoveryIntervalMs = 15000;

// Forward declarations for helpers used across the file (Arduino IDE auto-prototype
// generation does not see definitions inside an unnamed namespace).
void PublishStatusSnapshot();
void PublishProtocol(bool ok, const char* reason);
void RunProtocolDetection(const String& protocol_text);
void OnDetectProgress(const char* stage, obd::core::ObdProtocol candidate);
void HandleDetectProtocol(const String& raw_payload);
void PublishBusDiag(const char* trigger);
void PublishCapabilitiesSnapshot();
void HandleBusScan();
void HandleSetLink(const String& raw_payload);

void PublishStatusSnapshot() {
  const char* link_mode = g_twai.IsListenOnly() ? "listen_only" : "active";
  const char* stream_mode = g_scheduler.GetStreamMode() == obd::services::StreamMode::kRawFrames
                                ? "raw_frames"
                                : "telemetry";
  String payload = "{\"type\":\"status\",\"connected\":";
  payload += g_ble.IsConnected() ? "true" : "false";
  payload += ",\"mode\":\"";
  payload += link_mode;
  payload += "\",\"signal\":-55,\"stream_active\":";
  payload += g_scheduler.IsStreamingEnabled() ? "true" : "false";
  payload += ",\"link_baud_kbps\":";
  payload += String(g_twai.BaudKbps());
  payload += ",\"stream_mode\":\"";
  payload += stream_mode;
  payload += "\",\"diagnostic_profile\":\"";
  payload += g_transport.DiagnosticProfileIdString();
  payload += "\",\"protocol\":\"";
  payload += g_transport.ProtocolIdString();
  payload += "\",\"ts\":";
  payload += String(millis());
  payload += "}";
  g_ble.NotifyResponse(payload);
}

void PublishProtocol(bool ok, const char* reason) {
  String payload = "{\"type\":\"protocol\",\"ok\":";
  payload += ok ? "true" : "false";
  payload += ",\"protocol\":\"";
  payload += g_transport.ProtocolIdString();
  payload += "\",\"auto\":";
  payload += g_auto_protocol ? "true" : "false";
  if (reason != nullptr) {
    payload += ",\"reason\":\"";
    payload += reason;
    payload += "\"";
  }
  if (ok) {
    if (g_transport.IsCan()) {
      payload += ",\"baud_kbps\":";
      payload += String(g_twai.BaudKbps());
    }
    if (g_transport.SupportedPidsKnown()) {
      payload += ",\"supported_pids\":\"";
      payload += g_transport.SupportedPidsHex();
      payload += "\"";
    }
    const String vin = g_diagnostics.ReadVin();
    if (vin.length() > 0) {
      payload += ",\"vin\":\"";
      payload += vin;
      payload += "\"";
    }
  }
  payload += ",\"ts\":";
  payload += String(millis());
  payload += "}";
  g_ble.NotifyResponse(payload);
}

void OnDetectProgress(const char* stage, obd::core::ObdProtocol candidate) {
  String payload = "{\"type\":\"protocol_probe\",\"stage\":\"";
  payload += stage;
  payload += "\",\"protocol\":\"";
  payload += obd::core::ObdProtocolId(candidate);
  payload += "\",\"ts\":";
  payload += String(millis());
  payload += "}";
  g_ble.NotifyResponse(payload);
}

// Connects to the requested protocol ("auto" or a protocol id) and reports
// the outcome. Streaming is paused while the bus is being probed.
void RunProtocolDetection(const String& protocol_text) {
  obd::core::ObdProtocol requested = obd::core::ObdProtocol::kNone;
  bool is_auto = false;
  if (!obd::core::ParseObdProtocol(protocol_text, requested, is_auto)) {
    PublishProtocol(false, "unknown_protocol");
    return;
  }
  if (g_transport.GetDiagnosticProfile() != obd::core::DiagnosticProfile::kObdMode01) {
    PublishProtocol(false, "profile_not_obd");
    return;
  }
  if (!is_auto && (!obd::core::IsCanProtocol(requested) ||
                   requested == obd::core::ObdProtocol::kCan11Custom)) {
    // J1850 and K-line need bus drivers this board does not have.
    PublishProtocol(false, "unsupported_hardware");
    return;
  }

  const bool was_streaming = g_scheduler.IsStreamingEnabled();
  g_scheduler.SetStreamingEnabled(false);
  g_auto_protocol = is_auto;

  bool ok = false;
  if (is_auto) {
    ok = g_transport.AutoDetect(OnDetectProgress) != obd::core::ObdProtocol::kNone;
  } else {
    OnDetectProgress("probe", requested);
    ok = g_transport.Connect(requested);
    if (!ok) {
      // Leave the bus passive after a failed attempt.
      g_twai.BeginAt(g_twai.BaudKbps(), /*listen_only=*/true);
    }
  }
  PublishProtocol(ok, ok ? nullptr : "no_response");
  PublishStatusSnapshot();
  PublishCapabilitiesSnapshot();
  // Resume streaming even without a vehicle so the app keeps getting
  // (degraded) telemetry and CheckLinkHealth can retry in auto mode.
  if (was_streaming) {
    g_scheduler.SetStreamingEnabled(true);
  }
}

void HandleDetectProtocol(const String& raw_payload) {
  String protocol_text = ParseStringFieldLocal(raw_payload, "\"protocol\"");
  if (protocol_text.length() == 0) {
    protocol_text = "auto";
  }
  RunProtocolDetection(protocol_text);
}

void PublishBusDiag(const char* trigger) {
  String status = g_twai.GetStatusJson();
  if (status.length() == 0) {
    status = "{\"bus_state\":\"not_installed\"}";
  }
  String payload = "{\"type\":\"bus_diag\",\"trigger\":\"";
  payload += trigger;
  payload += "\",\"twai\":";
  payload += status;
  payload += ",\"ts\":";
  payload += String(millis());
  payload += "}";
  g_ble.NotifyResponse(payload);
}

void HandleBusScan() {
  // Auto-scan classical CAN baud rates in listen-only mode and report how many
  // valid frames each setting captures during a short window. Each baud result
  // is emitted as its own bus_scan_result notify so the payload always fits
  // inside the negotiated BLE MTU (no JSON truncation).
  static const uint32_t kCandidateBaud[] = {100, 125, 250, 500, 1000};
  static const uint8_t kCandidateCount = sizeof(kCandidateBaud) / sizeof(kCandidateBaud[0]);
  static const uint32_t kSampleWindowMs = 1500;

  const bool was_streaming = g_scheduler.IsStreamingEnabled();
  g_scheduler.SetStreamingEnabled(false);

  const uint32_t saved_baud = g_twai.BaudKbps();
  const bool saved_listen_only = g_twai.IsListenOnly();
  const obd::services::StreamMode saved_stream_mode = g_scheduler.GetStreamMode();

  {
    String start = "{\"type\":\"bus_scan_start\",\"count\":";
    start += String(kCandidateCount);
    start += ",\"window_ms\":";
    start += String(kSampleWindowMs);
    start += ",\"ts\":";
    start += String(millis());
    start += "}";
    g_ble.NotifyResponse(start);
  }

  for (uint8_t i = 0; i < kCandidateCount; ++i) {
    const uint32_t baud = kCandidateBaud[i];
    const bool installed = g_twai.BeginAt(baud, /*listen_only=*/true);

    uint32_t frames = 0;
    uint32_t bus_errors_start = 0;
    uint32_t bus_errors_end = 0;
    String first_id = "";
    if (installed) {
      twai_status_info_t info;
      if (twai_get_status_info(&info) == ESP_OK) {
        bus_errors_start = info.bus_error_count;
      }
      const uint32_t deadline = millis() + kSampleWindowMs;
      obd::drivers::CanFrame frame;
      while ((int32_t)(millis() - deadline) < 0) {
        if (g_twai.ReadFrame(frame, 50)) {
          if (frames == 0) {
            char buf[16];
            snprintf(buf, sizeof(buf), "0x%03lX", (unsigned long)frame.id);
            first_id = buf;
          }
          ++frames;
        }
      }
      if (twai_get_status_info(&info) == ESP_OK) {
        bus_errors_end = info.bus_error_count;
      }
    }

    String row = "{\"type\":\"bus_scan_result\",\"index\":";
    row += String(i);
    row += ",\"baud_kbps\":";
    row += String(baud);
    row += ",\"frames\":";
    row += String(frames);
    row += ",\"bus_errors_delta\":";
    row += String(bus_errors_end - bus_errors_start);
    row += ",\"installed\":";
    row += installed ? "true" : "false";
    if (first_id.length() > 0) {
      row += ",\"first_id\":\"";
      row += first_id;
      row += "\"";
    }
    row += ",\"ts\":";
    row += String(millis());
    row += "}";
    g_ble.NotifyResponse(row);
  }

  // Restore the link configuration from before the scan (typically the vehicle
  // profile from set_link). Do not leave the controller stuck on 500 kbps
  // listen-only — that caused confusing telemetry runs after a scan.
  g_twai.BeginAt(saved_baud, saved_listen_only);
  g_scheduler.SetStreamMode(saved_stream_mode);
  g_transport.Begin();

  {
    String done = "{\"type\":\"bus_scan_done\",\"streaming_was_enabled\":";
    done += was_streaming ? "true" : "false";
    done += ",\"ts\":";
    done += String(millis());
    done += "}";
    g_ble.NotifyResponse(done);
  }
  PublishStatusSnapshot();
}

void HandleSetLink(const String& raw_payload) {
  const uint32_t baud_kbps = ParseUintFieldLocal(raw_payload, "\"baud_kbps\"", 500);
  const String mode = ParseStringFieldLocal(raw_payload, "\"mode\"");
  const String stream_mode = ParseStringFieldLocal(raw_payload, "\"stream_mode\"");
  const bool listen_only = (mode == "listen_only");
  const bool raw_stream = (stream_mode == "raw_frames");

  g_scheduler.SetStreamingEnabled(false);
  const bool ok = g_twai.BeginAt(baud_kbps, listen_only);
  g_transport.Begin();
  const String protocol_text = ParseStringFieldLocal(raw_payload, "\"protocol\"");
  {
    const String diag = ParseStringFieldLocal(raw_payload, "\"diagnostic_profile\"");
    if (diag == "smart_ed" || diag == "smart_ed_uds" || diag == "smart_ed3") {
      g_transport.SetDiagnosticProfile(obd::core::DiagnosticProfile::kSmartEd);
      g_transport.Disconnect();
      g_smart_bms.Reset();
      // UDS rotation requires TX capability (Smart ED BMS diagnostic queries on
      // 0x7E7). In listen-only only the broadcast path is used.
      g_smart_bms.SetActiveMode(!listen_only);
    } else {
      g_transport.SetDiagnosticProfile(obd::core::DiagnosticProfile::kObdMode01);
      // Without a "protocol" field keep the original behaviour: 11-bit OBD
      // addressing on the requested baud, no verification.
      g_auto_protocol = false;
      g_transport.UseCanUnverified();
    }
  }
  g_scheduler.SetStreamMode(raw_stream ? obd::services::StreamMode::kRawFrames
                                       : obd::services::StreamMode::kTelemetry);

  String payload = "{\"type\":\"link\",\"ok\":";
  payload += ok ? "true" : "false";
  payload += ",\"baud_kbps\":";
  payload += String(g_twai.BaudKbps());
  payload += ",\"mode\":\"";
  payload += g_twai.IsListenOnly() ? "listen_only" : "active";
  payload += "\",\"stream_mode\":\"";
  payload += raw_stream ? "raw_frames" : "telemetry";
  payload += "\",\"diagnostic_profile\":\"";
  payload += g_transport.DiagnosticProfileIdString();
  payload += "\",\"ts\":";
  payload += String(millis());
  payload += "}";
  g_ble.NotifyResponse(payload);
  if (protocol_text.length() > 0 && !listen_only &&
      g_transport.GetDiagnosticProfile() == obd::core::DiagnosticProfile::kObdMode01) {
    // "protocol":"auto" (or a protocol id) asks for detection on top of set_link.
    RunProtocolDetection(protocol_text);
    return;
  }
  PublishStatusSnapshot();
  PublishCapabilitiesSnapshot();
}

void PublishCapabilitiesSnapshot() {
  if (g_transport.GetDiagnosticProfile() == obd::core::DiagnosticProfile::kSmartEd) {
    g_ble.NotifyResponse("{\"type\":\"capability\",\"supported_metrics\":["
                         "\"soc_pct\",\"soc_internal_pct\",\"hv_v\",\"hv_amps\","
                         "\"lv_v\",\"odo_km\",\"time_hh\",\"time_mm\""
                         "],\"missing_metrics\":[],"
                         "\"confirmed_from_vehicle\":false,\"ts\":" +
                         String(millis()) + "}");
    return;
  }
  // Reports the metrics the vehicle supports (from the PID 00/20/40/...
  // bitmaps) once a protocol is connected; before that, the whole firmware
  // catalogue. The list is split into parts so every notify fits the BLE MTU;
  // missing_metrics names the original four metrics the vehicle lacks.
  const bool confirmed = g_transport.SupportedPidsKnown();
  String missing;
  static const char* const kLegacyMetrics[] = {"rpm", "coolant_c", "throttle_pct", "speed_kmh"};
  String supported_chunks[24];
  uint8_t chunk_count = 0;
  String current;
  for (size_t i = 0; i < obd::core::kPidCatalogueLen; ++i) {
    const obd::core::PidField& field = obd::core::kPidCatalogue[i];
    bool legacy = false;
    for (const char* key : kLegacyMetrics) {
      legacy = legacy || strcmp(key, field.json_key) == 0;
    }
    if (!g_transport.IsPidSupported(field.pid)) {
      if (legacy) {
        missing += (missing.length() > 0 ? ",\"" : "\"");
        missing += field.json_key;
        missing += "\"";
      }
      continue;
    }
    if (current.length() + strlen(field.json_key) > 150 && chunk_count < 23) {
      supported_chunks[chunk_count++] = current;
      current = "";
    }
    current += (current.length() > 0 ? ",\"" : "\"");
    current += field.json_key;
    current += "\"";
  }
  supported_chunks[chunk_count++] = current;
  for (uint8_t i = 0; i < chunk_count; ++i) {
    String payload = "{\"type\":\"capability\",\"supported_metrics\":[";
    payload += supported_chunks[i];
    payload += "],\"missing_metrics\":[";
    if (i + 1 == chunk_count) {
      payload += missing;
    }
    payload += "],\"confirmed_from_vehicle\":";
    payload += confirmed ? "true" : "false";
    if (chunk_count > 1) {
      payload += ",\"part\":";
      payload += String(i + 1);
      payload += ",\"parts\":";
      payload += String(chunk_count);
    }
    payload += ",\"ts\":";
    payload += String(millis());
    payload += "}";
    g_ble.NotifyResponse(payload);
  }
}

void HandleBleCommand(const String& command, const String& raw_payload) {
  g_ble.NotifyResponse("{\"type\":\"log\",\"level\":\"info\",\"event\":\"command_received\","
                       "\"command\":\"" +
                       command + "\",\"raw_len\":" + String(raw_payload.length()) + ",\"ts\":" +
                       String(millis()) + "}");

  if (command == "start_stream") {
    obd::core::BitSet256 fields;
    obd::core::ParseTelemetryFields(raw_payload, fields);
    g_telemetry.SetTelemetryFields(fields);
    g_scheduler.SetStreamingEnabled(true);
    PublishStatusSnapshot();
    return;
  }

  if (command == "stop_stream") {
    g_scheduler.SetStreamingEnabled(false);
    PublishStatusSnapshot();
    return;
  }

  if (command == "read_dtc") {
    g_diagnostics.ReadDtc();
    return;
  }

  if (command == "read_freeze_frame") {
    g_diagnostics.ReadFreezeFrame();
    return;
  }

  if (command == "vehicle_info") {
    g_diagnostics.ReadVehicleInfo();
    return;
  }

  if (command == "probe_capabilities") {
    if (g_transport.IsConnected() && !g_transport.SupportedPidsKnown() &&
        g_transport.GetDiagnosticProfile() == obd::core::DiagnosticProfile::kObdMode01) {
      g_transport.DiscoverSupportedPids();
    }
    PublishCapabilitiesSnapshot();
    return;
  }

  if (command == "detect_protocol") {
    HandleDetectProtocol(raw_payload);
    return;
  }

  if (command == "set_link") {
    HandleSetLink(raw_payload);
    return;
  }

  if (command == "bus_diag") {
    PublishBusDiag("manual");
    return;
  }

  if (command == "bus_scan") {
    HandleBusScan();
    return;
  }
}

void EnqueueBleCommand(const String& command, const String& raw_payload) {
  PendingCommand item = {};
  strncpy(item.command, command.c_str(), sizeof(item.command) - 1);
  strncpy(item.payload, raw_payload.c_str(), sizeof(item.payload) - 1);
  if (g_command_queue == nullptr || xQueueSend(g_command_queue, &item, 0) != pdTRUE) {
    g_ble.NotifyResponse("{\"type\":\"log\",\"level\":\"warn\",\"event\":\"command_dropped\","
                         "\"command\":\"" +
                         command + "\",\"reason\":\"busy\",\"ts\":" + String(millis()) + "}");
  }
}

void DrainCommandQueue() {
  if (g_command_queue == nullptr) {
    return;
  }
  PendingCommand item;
  while (xQueueReceive(g_command_queue, &item, 0) == pdTRUE) {
    HandleBleCommand(String(item.command), String(item.payload));
  }
}

// When the ECU stops answering while streaming (ignition off, connector
// pulled, gateway asleep), report it and, if the protocol was auto-detected,
// try to find the vehicle again at a gentle pace. Retries only happen while
// the app is connected, so a parked car is never kept awake by probing.
void CheckLinkHealth() {
  if (!g_scheduler.IsStreamingEnabled() ||
      g_scheduler.GetStreamMode() != obd::services::StreamMode::kTelemetry ||
      g_transport.GetDiagnosticProfile() != obd::core::DiagnosticProfile::kObdMode01 ||
      millis() - g_last_link_lost_ms < kLinkRecoveryIntervalMs) {
    return;
  }
  const bool failing = g_transport.ConsecutiveFailures() >= kLinkLostFailures;
  const bool searching = g_auto_protocol && !g_transport.IsConnected();
  if (!failing && !searching) {
    return;
  }
  g_last_link_lost_ms = millis();
  if (searching) {
    if (g_ble.IsConnected()) {
      RunProtocolDetection("auto");
    }
    return;
  }
  g_ble.NotifyResponse("{\"type\":\"log\",\"level\":\"warn\",\"event\":\"link_lost\","
                       "\"protocol\":\"" +
                       String(g_transport.ProtocolIdString()) + "\",\"failures\":" +
                       String(g_transport.ConsecutiveFailures()) + ",\"ts\":" + String(millis()) + "}");
  if (g_auto_protocol && g_ble.IsConnected()) {
    RunProtocolDetection("auto");
  }
}
}  // namespace

void setup() {
  Serial.begin(115200);
  delay(300);

  Serial.println("ESP32 OBD firmware booting...");
  Serial.println(g_safety_guard.IsReadOnlyEnabled()
                     ? "Safety mode: listen-only/read-only enabled"
                     : "Safety mode: write-enabled");

  if (!g_can_phy.Begin()) {
    Serial.println("CAN PHY init failed");
  }
  // Boot in passive listen-only at 500 kbps so the ESP cannot transmit on a
  // bus until the app explicitly sets the correct baud and mode via set_link.
  // This protects motorcycles (K-CAN @ 100 kbps) and any unknown bus during
  // the first seconds after power-on.
  if (!g_twai.BeginAt(500, /*listen_only=*/true)) {
    Serial.println("TWAI listen-only init failed");
  } else {
    Serial.println("TWAI ready (listen-only, 500 kbps default; awaiting set_link)");
  }
  g_scheduler.SetStreamMode(obd::services::StreamMode::kRawFrames);
  if (!g_transport.Begin()) {
    Serial.println("OBD transport init failed");
  }
  if (!g_ble.Begin()) {
    Serial.println("BLE init failed");
  }
  g_command_queue = xQueueCreate(4, sizeof(PendingCommand));
  g_ble.SetCommandHandler(EnqueueBleCommand);

  g_scheduler.SetStreamingEnabled(false);
  PublishStatusSnapshot();
  PublishCapabilitiesSnapshot();
}

unsigned long g_last_diag_emit_ms = 0;

void loop() {
  DrainCommandQueue();
  g_transport.Tick();
  g_scheduler.Tick();
  CheckLinkHealth();
  g_connection_manager.Tick();
  // While streaming, emit a TWAI controller snapshot every 15 s. Helps diagnose
  // silent buses (bus_off, all-zero msgs_to_rx, growing tx_error) without
  // requiring the user to send a bus_diag command manually.
  if (g_scheduler.IsStreamingEnabled() && (millis() - g_last_diag_emit_ms) > 15000) {
    PublishBusDiag("auto_15s");
    g_last_diag_emit_ms = millis();
  }
  // OBD telemetry sends one request per pass, so keep the pass short there; raw
  // and Smart ED streams keep the original pacing.
  const bool polling_obd = g_scheduler.IsStreamingEnabled() &&
                           g_scheduler.GetStreamMode() == obd::services::StreamMode::kTelemetry &&
                           g_transport.GetDiagnosticProfile() == obd::core::DiagnosticProfile::kObdMode01;
  delay(polling_obd ? 10 : 100);
}
