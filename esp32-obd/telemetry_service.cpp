#include "telemetry_service.h"

namespace obd {
namespace services {

TelemetryService::TelemetryService(core::ObdTransport& transport,
                                   core::PidDecoder& pid_decoder,
                                   app_link::TransportBle& app_link)
    : transport_(transport), pid_decoder_(pid_decoder), app_link_(app_link) {}

void TelemetryService::Tick() {
  while (rotation_index_ < core::kPidCatalogueLen) {
    const core::PidDescriptor& entry = core::kPidCatalogue[rotation_index_];
    if ((telemetry_field_mask_ & entry.mask_bit) == 0) {
      ++rotation_index_;
      continue;
    }

    uint8_t payload[core::kPidMaxResponseBytes] = {0};
    uint8_t payload_len = 0;
    if (transport_.RequestPid(entry.pid, payload, payload_len)) {
      const String value = pid_decoder_.FormatValue(entry.pid, payload, payload_len);
      if (value.length() > 0) {
        if (!any_field_obtained_) {
          accumulated_json_ = "{\"type\":\"telemetry\",\"status\":\"ok\"";
          any_field_obtained_ = true;
        }
        accumulated_json_ += ",\"";
        accumulated_json_ += entry.json_key;
        accumulated_json_ += "\":";
        accumulated_json_ += value;
      }
    }

    ++rotation_index_;
    return;  // one PID per tick keeps the main loop responsive.
  }

  // Rotation done; publish what we collected (or a degraded stub if nothing).
  String payload;
  if (any_field_obtained_) {
    payload = accumulated_json_;
  } else {
    payload = "{\"type\":\"telemetry\",\"status\":\"degraded\"";
  }
  payload += ",\"ts\":";
  payload += String(millis());
  payload += "}";
  app_link_.NotifyTelemetry(payload);

  rotation_index_ = 0;
  accumulated_json_ = "";
  any_field_obtained_ = false;
}

void TelemetryService::SetTelemetryFieldMask(uint32_t mask) {
  telemetry_field_mask_ = (mask == 0) ? core::kTelemetryFieldAll : mask;
  Reset();
}

void TelemetryService::Reset() {
  rotation_index_ = 0;
  accumulated_json_ = "";
  any_field_obtained_ = false;
}

}  // namespace services
}  // namespace obd
