#include "telemetry_field_mask.h"

namespace obd {
namespace core {

namespace {

void SelectByKey(const String& key, BitSet256& fields) {
  for (size_t i = 0; i < kPidCatalogueLen; ++i) {
    if (key == kPidCatalogue[i].json_key) {
      fields.Set(static_cast<uint8_t>(i));
      return;
    }
  }
}

void ApplyMetricToken(const String& token, BitSet256& fields) {
  if (token == "all") {
    for (size_t i = 0; i < kPidCatalogueLen; ++i) {
      fields.Set(static_cast<uint8_t>(i));
    }
    return;
  }
  SelectByKey(token, fields);
}

String StripJsonQuotes(String token) {
  token.trim();
  if (token.length() >= 2 && token.charAt(0) == '"' && token.charAt(token.length() - 1) == '"') {
    return token.substring(1, token.length() - 1);
  }
  return token;
}

}  // namespace

void DefaultTelemetryFields(BitSet256& out_fields) {
  out_fields.Clear();
  SelectByKey("rpm", out_fields);
  SelectByKey("coolant_c", out_fields);
  SelectByKey("throttle_pct", out_fields);
  SelectByKey("speed_kmh", out_fields);
}

void ParseTelemetryFields(const String& raw_json, BitSet256& out_fields) {
  out_fields.Clear();
  const int key_pos = raw_json.indexOf("\"metrics\"");
  const int open_bracket = key_pos < 0 ? -1 : raw_json.indexOf('[', key_pos);
  const int close_bracket = open_bracket < 0 ? -1 : raw_json.indexOf(']', open_bracket);
  if (close_bracket > open_bracket) {
    int cursor = open_bracket + 1;
    while (cursor < close_bracket) {
      const int comma = raw_json.indexOf(',', cursor);
      const int end = (comma >= 0 && comma < close_bracket) ? comma : close_bracket;
      if (end > cursor) {
        ApplyMetricToken(StripJsonQuotes(raw_json.substring(cursor, end)), out_fields);
      }
      if (comma < 0 || comma >= close_bracket) {
        break;
      }
      cursor = comma + 1;
    }
  }
  if (out_fields.Empty()) {
    DefaultTelemetryFields(out_fields);
  }
}

}  // namespace core
}  // namespace obd
