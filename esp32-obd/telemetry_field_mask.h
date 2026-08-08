#pragma once

#include <Arduino.h>

namespace obd {
namespace core {

// Bitmask for optional telemetry JSON fields (beyond type/status/ts).
constexpr uint32_t kTelemetryFieldRpm = 1u << 0;
constexpr uint32_t kTelemetryFieldCoolantC = 1u << 1;
constexpr uint32_t kTelemetryFieldThrottlePct = 1u << 2;
constexpr uint32_t kTelemetryFieldSpeedKmh = 1u << 3;
constexpr uint32_t kTelemetryFieldAll =
    kTelemetryFieldRpm | kTelemetryFieldCoolantC | kTelemetryFieldThrottlePct | kTelemetryFieldSpeedKmh;

// Parses optional "metrics":["rpm",...] from command JSON. Returns kTelemetryFieldAll if absent.
uint32_t ParseTelemetryFieldMask(const String& raw_json);

}  // namespace core
}  // namespace obd
