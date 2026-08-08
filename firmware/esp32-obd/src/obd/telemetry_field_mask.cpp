#include "../../include/obd/telemetry_field_mask.h"

namespace obd {
namespace core {

namespace {

void ApplyMetricToken(const String& token, uint32_t& mask) {
  if (token == "rpm") {
    mask |= kTelemetryFieldRpm;
  } else if (token == "coolant_c") {
    mask |= kTelemetryFieldCoolantC;
  } else if (token == "throttle_pct") {
    mask |= kTelemetryFieldThrottlePct;
  } else if (token == "speed_kmh") {
    mask |= kTelemetryFieldSpeedKmh;
  }
}

String StripJsonQuotes(String token) {
  token.trim();
  if (token.length() >= 2 && token.charAt(0) == '"' && token.charAt(token.length() - 1) == '"') {
    return token.substring(1, token.length() - 1);
  }
  return token;
}

}  // namespace

uint32_t ParseTelemetryFieldMask(const String& raw_json) {
  const int key_pos = raw_json.indexOf("\"metrics\"");
  if (key_pos < 0) {
    return kTelemetryFieldAll;
  }

  const int open_bracket = raw_json.indexOf('[', key_pos);
  if (open_bracket < 0) {
    return kTelemetryFieldAll;
  }

  const int close_bracket = raw_json.indexOf(']', open_bracket);
  if (close_bracket < 0 || close_bracket <= open_bracket) {
    return kTelemetryFieldAll;
  }

  uint32_t mask = 0;
  int cursor = open_bracket + 1;
  while (cursor < close_bracket) {
    const int comma = raw_json.indexOf(',', cursor);
    const int end = (comma >= 0 && comma < close_bracket) ? comma : close_bracket;
    if (end > cursor) {
      ApplyMetricToken(StripJsonQuotes(raw_json.substring(cursor, end)), mask);
    }
    if (comma < 0 || comma >= close_bracket) {
      break;
    }
    cursor = comma + 1;
  }

  if (mask == 0) {
    return kTelemetryFieldAll;
  }
  return mask;
}

}  // namespace core
}  // namespace obd
