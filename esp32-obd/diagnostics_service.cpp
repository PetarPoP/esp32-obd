#include "diagnostics_service.h"

namespace obd {
namespace services {

namespace {
// Combined length of the three code lists that still fits one BLE notify.
constexpr size_t kMaxCodeListChars = 170;
constexpr uint32_t kDtcResponseTimeoutMs = 300;
}  // namespace

DiagnosticsService::DiagnosticsService(core::ObdTransport& transport,
                                       core::DtcDecoder& dtc_decoder,
                                       app_link::TransportBle& app_link,
                                       safety::SafetyGuard& safety_guard)
    : transport_(transport),
      dtc_decoder_(dtc_decoder),
      app_link_(app_link),
      safety_guard_(safety_guard) {}

bool DiagnosticsService::ReadMode(uint8_t mode, String& list, bool& truncated) {
  const uint8_t count =
      transport_.Request(&mode, 1, /*wait_for_all=*/true, transport_.IsCan() ? kDtcResponseTimeoutMs : 0);
  // CAN answers carry a DTC count byte after the SID; K-line answers do not.
  const uint8_t first_pair = transport_.IsCan() ? 2 : 1;
  for (uint8_t m = 0; m < count; ++m) {
    const core::ObdMessage& msg = transport_.Message(m);
    for (uint8_t i = first_pair; i + 1 < msg.len; i += 2) {
      const String code = core::DtcDecoder::Format(msg.data[i], msg.data[i + 1]);
      if (code.length() == 0) {
        continue;
      }
      const String quoted = "\"" + dtc_decoder_.DecodeOrUnknown(code) + "\"";
      if (list.indexOf(quoted) >= 0) {
        continue;
      }
      if (code_list_chars_ + quoted.length() + 1 > kMaxCodeListChars) {
        truncated = true;
        continue;
      }
      if (list.length() > 0) {
        list += ",";
      }
      list += quoted;
      code_list_chars_ += quoted.length() + 1;
    }
  }
  return count > 0;
}

void DiagnosticsService::ReadDtc() {
  if (!transport_.IsConnected() ||
      transport_.GetDiagnosticProfile() != core::DiagnosticProfile::kObdMode01) {
    app_link_.NotifyResponse("{\"type\":\"dtc\",\"status\":\"no_link\",\"codes\":[],\"ts\":" +
                             String(millis()) + "}");
    return;
  }

  bool truncated = false;
  code_list_chars_ = 0;
  String stored;
  String pending;
  String permanent;
  const bool stored_ok = ReadMode(0x03, stored, truncated);
  ReadMode(0x07, pending, truncated);
  // Permanent DTCs (Mode 0A) only exist on 2010+ vehicles; no answer is normal.
  ReadMode(0x0A, permanent, truncated);

  String payload = "{\"type\":\"dtc\",\"status\":\"";
  payload += stored_ok ? "ok" : "no_response";
  payload += "\",\"codes\":[";
  payload += stored;
  payload += "],\"pending\":[";
  payload += pending;
  payload += "],\"permanent\":[";
  payload += permanent;
  payload += "]";
  if (truncated) {
    payload += ",\"truncated\":true";
  }
  payload += ",\"ts\":";
  payload += String(millis());
  payload += "}";
  app_link_.NotifyResponse(payload);
}

void DiagnosticsService::ClearDtc() {
  if (!safety_guard_.IsClearDtcAllowed()) {
    app_link_.NotifyResponse(
        "{\"type\":\"ack\",\"command\":\"clear_dtc\",\"status\":\"locked\","
        "\"reason\":\"safety_lock\",\"ts\":" +
        String(millis()) + "}");
    return;
  }
  app_link_.NotifyResponse(
      "{\"type\":\"ack\",\"command\":\"clear_dtc\",\"status\":\"ok\",\"ts\":" +
      String(millis()) + "}");
}

}  // namespace services
}  // namespace obd
