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
obd::services::DiagnosticsService g_diagnostics(g_dtc_decoder, g_ble, g_safety_guard);
obd::services::ConnectionManager g_connection_manager;
obd::services::ObdSchedulerService g_scheduler(g_transport, g_telemetry, g_raw_frames, g_smart_bms);

// Forward declarations for helpers used across the file (Arduino IDE auto-prototype
// generation does not see definitions inside an unnamed namespace).
void PublishStatusSnapshot();
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
  payload += "\",\"ts\":";
  payload += String(millis());
  payload += "}";
  g_ble.NotifyResponse(payload);
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
  {
    const String diag = ParseStringFieldLocal(raw_payload, "\"diagnostic_profile\"");
    if (diag == "smart_ed" || diag == "smart_ed_uds" || diag == "smart_ed3") {
      g_transport.SetDiagnosticProfile(obd::core::DiagnosticProfile::kSmartEd);
      g_smart_bms.Reset();
      // UDS rotation requires TX capability (Smart ED BMS diagnostic queries on
      // 0x7E7). In listen-only only the broadcast path is used.
      g_smart_bms.SetActiveMode(!listen_only);
    } else {
      g_transport.SetDiagnosticProfile(obd::core::DiagnosticProfile::kObdMode01);
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
  // Reports the PIDs this firmware knows how to decode. The "supported" list is
  // not yet probed from the vehicle (that requires PID 0x00/0x20/0x40 bitmap
  // parsing); for now it equals the firmware catalogue.
  String payload = "{\"type\":\"capability\",\"supported_metrics\":[";
  for (size_t i = 0; i < obd::core::kPidCatalogueLen; ++i) {
    if (i > 0) {
      payload += ",";
    }
    payload += "\"";
    payload += obd::core::kPidCatalogue[i].json_key;
    payload += "\"";
  }
  payload += "],\"missing_metrics\":[],\"confirmed_from_vehicle\":false,\"ts\":";
  payload += String(millis());
  payload += "}";
  g_ble.NotifyResponse(payload);
}

void HandleBleCommand(const String& command, const String& raw_payload) {
  g_ble.NotifyResponse("{\"type\":\"log\",\"level\":\"info\",\"event\":\"command_received\","
                       "\"command\":\"" +
                       command + "\",\"raw_len\":" + String(raw_payload.length()) + ",\"ts\":" +
                       String(millis()) + "}");

  if (command == "start_stream") {
    g_telemetry.SetTelemetryFieldMask(obd::core::ParseTelemetryFieldMask(raw_payload));
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

  if (command == "probe_capabilities") {
    PublishCapabilitiesSnapshot();
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
}  // namespace

void setup() {
  Serial.begin(115200);
  delay(300);

  Serial.println("ESP32 OBD firmware skeleton booting...");
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
  g_ble.SetCommandHandler(HandleBleCommand);

  g_scheduler.SetStreamingEnabled(false);
  PublishStatusSnapshot();
  PublishCapabilitiesSnapshot();
}

unsigned long g_last_diag_emit_ms = 0;

void loop() {
  g_scheduler.Tick();
  g_connection_manager.Tick();
  // While streaming, emit a TWAI controller snapshot every 5 s. Helps diagnose
  // silent buses (bus_off, all-zero msgs_to_rx, growing tx_error) without
  // requiring the user to send a bus_diag command manually.
  if (g_scheduler.IsStreamingEnabled() && (millis() - g_last_diag_emit_ms) > 15000) {
    PublishBusDiag("auto_15s");
    g_last_diag_emit_ms = millis();
  }
  delay(100);
}
