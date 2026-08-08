#include "../../include/obd/pid_decoder.h"

namespace obd {
namespace core {

const PidDescriptor kPidCatalogue[] = {
    {kTelemetryFieldRpm, 0x0C, "rpm"},
    {kTelemetryFieldSpeedKmh, 0x0D, "speed_kmh"},
    {kTelemetryFieldCoolantC, 0x05, "coolant_c"},
    // PID 0x49 (Accelerator pedal position D) tracks the gas pedal directly on
    // drive-by-wire cars; PID 0x11 (Throttle valve position) is what the ECU
    // commands to the valve and on many ECUs sits ~constant at idle / cruise.
    {kTelemetryFieldThrottlePct, 0x49, "throttle_pct"},
};
const size_t kPidCatalogueLen = sizeof(kPidCatalogue) / sizeof(kPidCatalogue[0]);

String PidDecoder::FormatValue(uint8_t pid, const uint8_t* payload, uint8_t payload_len) const {
  if (payload == nullptr || payload_len == 0) {
    return String();
  }

  switch (pid) {
    case 0x05: {  // Engine coolant temperature (deg C); 0xFF = not available
      if (payload[0] == 0xFF) {
        return String();
      }
      return String(static_cast<int>(payload[0]) - 40);
    }
    case 0x0C: {  // Engine RPM; 0xFFFF = not available
      if (payload_len < 2) {
        return String();
      }
      const uint16_t raw = (static_cast<uint16_t>(payload[0]) << 8) | payload[1];
      if (raw == 0xFFFF) {
        return String();
      }
      return String(raw / 4);
    }
    case 0x0D: {  // Vehicle speed (km/h); 0xFF = not available
      if (payload[0] == 0xFF) {
        return String();
      }
      return String(static_cast<int>(payload[0]));
    }
    case 0x49: {  // Accelerator pedal position D; 0xFF = not available
      if (payload[0] == 0xFF) {
        return String();
      }
      return String(static_cast<int>((static_cast<uint32_t>(payload[0]) * 100u) / 255u));
    }
    default:
      return String();
  }
}

}  // namespace core
}  // namespace obd
