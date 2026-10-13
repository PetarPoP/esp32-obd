#include "diagnostics_service.h"

#include <esp_random.h>

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

void DiagnosticsService::ClearDtc(const String& confirm_token) {
  auto reply = [&](const char* status, const String& extra) {
    app_link_.NotifyResponse(String("{\"type\":\"clear_dtc\",\"status\":\"") + status + "\"" + extra +
                             ",\"ts\":" + String(millis()) + "}");
  };
  if (!safety_guard_.IsClearDtcAllowed()) {
    reply("locked", ",\"reason\":\"safety_lock\"");
    return;
  }
  if (!IsObdReady()) {
    reply("no_link", "");
    return;
  }
  // Only with the vehicle stationary and the engine off (ignition on).
  uint8_t data[core::kPidMaxResponseBytes];
  uint8_t len = 0;
  const bool speed_ok = transport_.RequestPid(0x0D, data, len) && len >= 1 && data[0] == 0;
  const bool rpm_ok = transport_.RequestPid(0x0C, data, len) && len >= 2 && data[0] == 0 && data[1] < 4;
  if (!speed_ok || !rpm_ok) {
    clear_token_ = 0;
    reply("refused", ",\"reason\":\"vehicle_must_be_stopped_engine_off\"");
    return;
  }

  const uint32_t now = millis();
  const bool token_valid = clear_token_ != 0 && (int32_t)(clear_token_expires_ms_ - now) > 0;
  if (confirm_token.length() == 0 || !token_valid || confirm_token.toInt() != static_cast<long>(clear_token_)) {
    // Step 1: hand out a short-lived token the app must send back after the
    // user confirmed. Clearing also resets readiness monitors.
    clear_token_ = 1000 + (esp_random() % 9000);
    clear_token_expires_ms_ = now + kClearTokenLifetimeMs;
    reply("confirm_required", ",\"token\":\"" + String(clear_token_) + "\",\"expires_s\":" +
                                  String(kClearTokenLifetimeMs / 1000));
    return;
  }

  clear_token_ = 0;
  const uint8_t request = 0x04;
  const uint8_t count = transport_.Request(&request, 1, /*wait_for_all=*/true, 1000);
  if (count > 0) {
    reply("cleared", ",\"ecus\":" + String(count));
  } else {
    const uint8_t nrc = transport_.LastNegativeResponseCode();
    reply("failed", nrc != 0 ? ",\"nrc\":" + String(nrc) : String());
  }
}

void DiagnosticsService::ReadReadiness() {
  if (!IsObdReady()) {
    app_link_.NotifyResponse("{\"type\":\"readiness\",\"status\":\"no_link\",\"ts\":" + String(millis()) + "}");
    return;
  }
  uint8_t d[core::kPidMaxResponseBytes];
  uint8_t len = 0;
  if (!transport_.RequestPid(0x01, d, len) || len < 4) {
    app_link_.NotifyResponse("{\"type\":\"readiness\",\"status\":\"no_response\",\"ts\":" + String(millis()) + "}");
    return;
  }
  // SAE J1979 PID 01: A = MIL + DTC count; B = common monitors and ignition
  // type; C = available, D = incomplete monitors (spark or compression set).
  static const char* const kCommon[3] = {"misfire", "fuel", "components"};
  static const char* const kSpark[8] = {"catalyst", "heated_cat", "evap", "sec_air",
                                        "ac", "o2", "o2_heater", "egr"};
  static const char* const kDiesel[8] = {"nmhc_cat", "nox_scr", nullptr, "boost",
                                         nullptr, "exhaust_sensor", "pm_filter", "egr_vvt"};
  const bool diesel = (d[1] & 0x08) != 0;
  String complete;
  String incomplete;
  auto add = [](String& list, const char* name) {
    list += (list.length() > 0 ? ",\"" : "\"");
    list += name;
    list += "\"";
  };
  for (uint8_t i = 0; i < 3; ++i) {
    if (d[1] & (1 << i)) {
      add((d[1] & (1 << (i + 4))) ? incomplete : complete, kCommon[i]);
    }
  }
  for (uint8_t i = 0; i < 8; ++i) {
    const char* name = diesel ? kDiesel[i] : kSpark[i];
    if (name != nullptr && (d[2] & (1 << i))) {
      add((d[3] & (1 << i)) ? incomplete : complete, name);
    }
  }
  String payload = "{\"type\":\"readiness\",\"status\":\"ok\",\"mil\":";
  payload += (d[0] & 0x80) ? "true" : "false";
  payload += ",\"dtc_count\":";
  payload += String(d[0] & 0x7F);
  payload += ",\"ignition\":\"";
  payload += diesel ? "compression" : "spark";
  payload += "\",\"complete\":[";
  payload += complete;
  payload += "],\"incomplete\":[";
  payload += incomplete;
  payload += "],\"ts\":";
  payload += String(millis());
  payload += "}";
  app_link_.NotifyResponse(payload);
}

namespace {

struct UasScaling {
  uint8_t id;
  float scale;
  float offset;
  const char* unit;
};

// SAE J1979 Unit and Scaling IDs used by Mode 06 test results (common subset;
// unknown IDs are reported raw). IDs from 0x80 are two's complement values.
const UasScaling kUasTable[] = {
    {0x01, 1.0f, 0.0f, ""},        {0x02, 0.1f, 0.0f, ""},        {0x03, 0.01f, 0.0f, ""},
    {0x04, 0.001f, 0.0f, ""},      {0x05, 0.0000305f, 0.0f, ""},  {0x06, 0.000305f, 0.0f, ""},
    {0x07, 0.25f, 0.0f, "rpm"},    {0x08, 0.01f, 0.0f, "km/h"},   {0x09, 1.0f, 0.0f, "km/h"},
    {0x0A, 0.000122f, 0.0f, "V"},  {0x0B, 0.001f, 0.0f, "V"},     {0x0C, 0.01f, 0.0f, "V"},
    {0x0D, 0.00390625f, 0.0f, "mA"}, {0x0E, 0.001f, 0.0f, "A"},   {0x0F, 0.01f, 0.0f, "A"},
    {0x10, 1.0f, 0.0f, "ms"},      {0x11, 100.0f, 0.0f, "ms"},    {0x12, 1.0f, 0.0f, "s"},
    {0x13, 1.0f, 0.0f, "mOhm"},    {0x14, 1.0f, 0.0f, "Ohm"},     {0x15, 1.0f, 0.0f, "kOhm"},
    {0x16, 0.1f, -40.0f, "C"},     {0x17, 0.01f, 0.0f, "kPa"},    {0x18, 0.0117f, 0.0f, "kPa"},
    {0x19, 0.079f, 0.0f, "kPa"},   {0x1A, 1.0f, 0.0f, "kPa"},     {0x1B, 10.0f, 0.0f, "kPa"},
    {0x1C, 0.01f, 0.0f, "deg"},    {0x1D, 0.5f, 0.0f, "deg"},     {0x1E, 0.0000305f, 0.0f, "lambda"},
    {0x1F, 0.05f, 0.0f, "AFR"},    {0x20, 0.0039062f, 0.0f, ""},  {0x22, 1.0f, 0.0f, "Hz"},
    {0x24, 1.0f, 0.0f, "counts"},  {0x25, 1.0f, 0.0f, "km"},      {0x27, 0.01f, 0.0f, "g/s"},
    {0x28, 1.0f, 0.0f, "g/s"},     {0x2F, 0.01f, 0.0f, "%"},      {0x34, 1.0f, 0.0f, "min"},
    {0x81, 1.0f, 0.0f, ""},        {0x82, 0.1f, 0.0f, ""},        {0x83, 0.01f, 0.0f, ""},
    {0x84, 0.001f, 0.0f, ""},      {0x85, 0.0000305f, 0.0f, ""},  {0x86, 0.000305f, 0.0f, ""},
    {0x8A, 0.000122f, 0.0f, "V"},  {0x8B, 0.001f, 0.0f, "V"},     {0x8C, 0.01f, 0.0f, "V"},
    {0x8D, 0.00390625f, 0.0f, "mA"}, {0x8E, 0.001f, 0.0f, "A"},   {0x90, 1.0f, 0.0f, "ms"},
    {0x96, 0.1f, 0.0f, "C"},       {0x9C, 0.01f, 0.0f, "deg"},    {0x9D, 0.5f, 0.0f, "deg"},
    {0xAF, 0.01f, 0.0f, "%"},      {0xFC, 0.01f, 0.0f, "kPa"},    {0xFD, 0.001f, 0.0f, "kPa"},
    {0xFE, 0.25f, 0.0f, "Pa"},
};

String Hex2(uint8_t v) {
  char buf[3];
  snprintf(buf, sizeof(buf), "%02X", v);
  return String(buf);
}

}  // namespace

void DiagnosticsService::ReadMonitorTests() {
  if (!IsObdReady()) {
    app_link_.NotifyResponse("{\"type\":\"monitor_tests_done\",\"status\":\"no_link\",\"count\":0,\"ts\":" +
                             String(millis()) + "}");
    return;
  }
  // Supported OBD monitor IDs, same bitmap scheme as Mode 01 PIDs.
  uint32_t supported[8] = {0};
  for (uint16_t base = 0; base <= 0xE0; base += 0x20) {
    const uint8_t request[2] = {0x06, static_cast<uint8_t>(base)};
    const uint8_t count = transport_.Request(request, sizeof(request), /*wait_for_all=*/true);
    for (uint8_t m = 0; m < count; ++m) {
      const core::ObdMessage& msg = transport_.Message(m);
      if (msg.len >= 6 && msg.data[1] == base) {
        supported[base / 0x20] |= (static_cast<uint32_t>(msg.data[2]) << 24) |
                                  (static_cast<uint32_t>(msg.data[3]) << 16) |
                                  (static_cast<uint32_t>(msg.data[4]) << 8) | msg.data[5];
      }
    }
    if (count == 0 || (supported[base / 0x20] & 0x01) == 0) {
      break;
    }
  }

  uint16_t results = 0;
  for (uint16_t mid = 1; mid <= 0xFF; ++mid) {
    if (mid % 0x20 == 0) {
      continue;
    }
    const uint8_t index = (mid - 1) / 32;
    const uint8_t bit = 31 - ((mid - 1) % 32);
    if (((supported[index] >> bit) & 0x01) == 0) {
      continue;
    }
    const uint8_t request[2] = {0x06, static_cast<uint8_t>(mid)};
    const uint8_t count = transport_.Request(request, sizeof(request), /*wait_for_all=*/true);
    for (uint8_t m = 0; m < count; ++m) {
      const core::ObdMessage& msg = transport_.Message(m);
      // 46 then records of 9 bytes: MID TID UAS value(2) min(2) max(2).
      for (uint8_t i = 1; i + 9 <= msg.len; i += 9) {
        const uint8_t* r = &msg.data[i];
        if (r[0] != mid) {
          continue;
        }
        const uint8_t uas = r[2];
        const uint16_t raw_v = (r[3] << 8) | r[4];
        const uint16_t raw_min = (r[5] << 8) | r[6];
        const uint16_t raw_max = (r[7] << 8) | r[8];
        const bool is_signed = uas >= 0x80;
        auto as_num = [&](uint16_t raw) {
          return is_signed ? static_cast<float>(static_cast<int16_t>(raw)) : static_cast<float>(raw);
        };
        const float v = as_num(raw_v), lo = as_num(raw_min), hi = as_num(raw_max);
        const bool pass = v >= lo && v <= hi;
        String payload = "{\"type\":\"monitor_test\",\"ecu\":\"" + Hex2(msg.ecu) + "\",\"mid\":\"" + Hex2(r[0]) +
                         "\",\"tid\":\"" + Hex2(r[1]) + "\",\"uas\":\"" + Hex2(uas) + "\",\"pass\":" +
                         (pass ? "true" : "false");
        const UasScaling* scaling = nullptr;
        for (const UasScaling& s : kUasTable) {
          if (s.id == uas) {
            scaling = &s;
            break;
          }
        }
        if (scaling != nullptr) {
          payload += ",\"value\":" + String(v * scaling->scale + scaling->offset, 4) +
                     ",\"min\":" + String(lo * scaling->scale + scaling->offset, 4) +
                     ",\"max\":" + String(hi * scaling->scale + scaling->offset, 4) + ",\"unit\":\"" +
                     scaling->unit + "\"";
        } else {
          payload += ",\"raw\":" + String(raw_v) + ",\"raw_min\":" + String(raw_min) +
                     ",\"raw_max\":" + String(raw_max);
        }
        payload += ",\"ts\":" + String(millis()) + "}";
        app_link_.NotifyResponse(payload);
        ++results;
      }
    }
  }
  app_link_.NotifyResponse("{\"type\":\"monitor_tests_done\",\"status\":\"ok\",\"count\":" + String(results) +
                           ",\"ts\":" + String(millis()) + "}");
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
