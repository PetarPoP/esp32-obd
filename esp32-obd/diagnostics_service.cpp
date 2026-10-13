#include "diagnostics_service.h"

namespace obd {
namespace services {

namespace {
// Combined length of the three code lists that still fits one BLE notify.
constexpr size_t kMaxCodeListChars = 170;
// JSON body length at which a freeze frame message is split (BLE MTU).
constexpr size_t kMaxPayloadBody = 190;
constexpr uint32_t kMultiEcuTimeoutMs = 300;

// PIDs that ECUs commonly store in freeze frames.
const uint8_t kFreezeFramePids[] = {0x03, 0x04, 0x05, 0x06, 0x07, 0x0A, 0x0B,
                                    0x0C, 0x0D, 0x0E, 0x0F, 0x10, 0x11, 0x1F};

// Printable ASCII from data[offset, offset + len), trimmed.
String AsciiField(const core::ObdMessage& msg, uint8_t offset, uint8_t len) {
  String text;
  for (uint8_t i = offset; i < msg.len && i < offset + len; ++i) {
    const uint8_t c = msg.data[i];
    if (c >= 0x20 && c < 0x7F && c != '"' && c != '\\') {
      text += static_cast<char>(c);
    }
  }
  text.trim();
  return text;
}

}  // namespace

DiagnosticsService::DiagnosticsService(core::ObdTransport& transport,
                                       core::DtcDecoder& dtc_decoder,
                                       core::PidDecoder& pid_decoder,
                                       app_link::TransportBle& app_link,
                                       safety::SafetyGuard& safety_guard)
    : transport_(transport),
      dtc_decoder_(dtc_decoder),
      pid_decoder_(pid_decoder),
      app_link_(app_link),
      safety_guard_(safety_guard) {}

bool DiagnosticsService::IsObdReady() const {
  return transport_.IsConnected() &&
         transport_.GetDiagnosticProfile() == core::DiagnosticProfile::kObdMode01;
}

bool DiagnosticsService::ReadMode(uint8_t mode, String& list, bool& truncated) {
  const uint8_t count = transport_.Request(&mode, 1, /*wait_for_all=*/true, kMultiEcuTimeoutMs);
  for (uint8_t m = 0; m < count; ++m) {
    const core::ObdMessage& msg = transport_.Message(m);
    // CAN answers: <SID> <count> <pairs...>.
    for (uint8_t i = 2; i + 1 < msg.len; i += 2) {
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
  if (!IsObdReady()) {
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

void DiagnosticsService::ReadFreezeFrame() {
  if (!IsObdReady()) {
    app_link_.NotifyResponse("{\"type\":\"freeze_frame\",\"status\":\"no_link\",\"ts\":" +
                             String(millis()) + "}");
    return;
  }

  // 02 02 00 -> 42 02 00 <DTC hi> <DTC lo>
  const uint8_t dtc_request[3] = {0x02, 0x02, 0x00};
  String dtc;
  if (transport_.Request(dtc_request, sizeof(dtc_request), /*wait_for_all=*/false) > 0) {
    const core::ObdMessage& msg = transport_.Message(0);
    if (msg.len >= 5) {
      dtc = core::DtcDecoder::Format(msg.data[3], msg.data[4]);
    }
  }
  if (dtc.length() == 0) {
    app_link_.NotifyResponse("{\"type\":\"freeze_frame\",\"status\":\"none\",\"ts\":" +
                             String(millis()) + "}");
    return;
  }

  const String header = "{\"type\":\"freeze_frame\",\"status\":\"ok\",\"dtc\":\"" + dtc + "\"";
  String body = header;
  for (uint8_t pid : kFreezeFramePids) {
    const uint8_t request[3] = {0x02, pid, 0x00};
    if (transport_.Request(request, sizeof(request), /*wait_for_all=*/false) == 0) {
      continue;
    }
    // 42 <pid> <frame> <data...>
    const core::ObdMessage& msg = transport_.Message(0);
    if (msg.len <= 3) {
      continue;
    }
    for (size_t i = 0; i < core::kPidCatalogueLen; ++i) {
      const core::PidField& field = core::kPidCatalogue[i];
      if (field.pid != pid) {
        continue;
      }
      const String value = pid_decoder_.FormatField(field, &msg.data[3], msg.len - 3);
      if (value.length() == 0) {
        continue;
      }
      if (body.length() + strlen(field.json_key) + value.length() > kMaxPayloadBody) {
        app_link_.NotifyResponse(body + ",\"partial\":true,\"ts\":" + String(millis()) + "}");
        body = header;
      }
      body += ",\"";
      body += field.json_key;
      body += "\":";
      body += value;
    }
  }
  app_link_.NotifyResponse(body + ",\"ts\":" + String(millis()) + "}");
}

String DiagnosticsService::ReadVin() {
  const uint8_t request[2] = {0x09, 0x02};
  if (transport_.Request(request, sizeof(request), /*wait_for_all=*/false, kMultiEcuTimeoutMs) == 0) {
    return String();
  }
  // 49 02 <count> <17 ASCII bytes>
  String vin = AsciiField(transport_.Message(0), 3, 17);
  if (vin.length() > 17) {
    vin = vin.substring(vin.length() - 17);
  }
  return vin;
}

void DiagnosticsService::ReadVehicleInfo() {
  if (!IsObdReady()) {
    app_link_.NotifyResponse("{\"type\":\"vehicle_info\",\"status\":\"no_link\",\"ts\":" +
                             String(millis()) + "}");
    return;
  }

  String payload = "{\"type\":\"vehicle_info\",\"status\":\"ok\",\"vin\":\"";
  payload += ReadVin();
  payload += "\"";

  // 49 04 <count> <16 bytes per calibration ID>, one message per ECU.
  const uint8_t cal_request[2] = {0x09, 0x04};
  String cal_ids;
  const uint8_t cal_count =
      transport_.Request(cal_request, sizeof(cal_request), /*wait_for_all=*/true, kMultiEcuTimeoutMs);
  for (uint8_t m = 0; m < cal_count && m < 3; ++m) {
    const String cal = AsciiField(transport_.Message(m), 3, 16);
    if (cal.length() > 0) {
      cal_ids += (cal_ids.length() > 0 ? ",\"" : "\"") + cal + "\"";
    }
  }
  payload += ",\"cal_ids\":[" + cal_ids + "]";

  // 49 0A 01 <20 ASCII bytes>, one message per ECU.
  const uint8_t name_request[2] = {0x09, 0x0A};
  String names;
  const uint8_t name_count =
      transport_.Request(name_request, sizeof(name_request), /*wait_for_all=*/true, kMultiEcuTimeoutMs);
  for (uint8_t m = 0; m < name_count && m < 3; ++m) {
    const String name = AsciiField(transport_.Message(m), 3, 20);
    if (name.length() > 0) {
      names += (names.length() > 0 ? ",\"" : "\"") + name + "\"";
    }
  }
  payload += ",\"ecu_names\":[" + names + "]";
  payload += ",\"ts\":";
  payload += String(millis());
  payload += "}";
  app_link_.NotifyResponse(payload);
}

}  // namespace services
}  // namespace obd
