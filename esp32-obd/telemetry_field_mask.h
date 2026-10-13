#pragma once

#include <Arduino.h>

namespace obd {
namespace core {

// Bitmask for optional telemetry JSON fields (beyond type/status/ts). The
// first four bits keep their original meaning for app compatibility.
constexpr uint32_t kTelemetryFieldRpm = 1u << 0;
constexpr uint32_t kTelemetryFieldCoolantC = 1u << 1;
constexpr uint32_t kTelemetryFieldThrottlePct = 1u << 2;
constexpr uint32_t kTelemetryFieldSpeedKmh = 1u << 3;
constexpr uint32_t kTelemetryFieldEngineLoadPct = 1u << 4;
constexpr uint32_t kTelemetryFieldIntakeAirC = 1u << 5;
constexpr uint32_t kTelemetryFieldMafGs = 1u << 6;
constexpr uint32_t kTelemetryFieldMapKpa = 1u << 7;
constexpr uint32_t kTelemetryFieldTimingAdvanceDeg = 1u << 8;
constexpr uint32_t kTelemetryFieldThrottleValvePct = 1u << 9;
constexpr uint32_t kTelemetryFieldFuelLevelPct = 1u << 10;
constexpr uint32_t kTelemetryFieldBaroKpa = 1u << 11;
constexpr uint32_t kTelemetryFieldModuleVoltageV = 1u << 12;
constexpr uint32_t kTelemetryFieldAmbientAirC = 1u << 13;
constexpr uint32_t kTelemetryFieldOilTempC = 1u << 14;
constexpr uint32_t kTelemetryFieldFuelRateLh = 1u << 15;
constexpr uint32_t kTelemetryFieldStftB1Pct = 1u << 16;
constexpr uint32_t kTelemetryFieldLtftB1Pct = 1u << 17;
constexpr uint32_t kTelemetryFieldFuelPressureKpa = 1u << 18;
constexpr uint32_t kTelemetryFieldRuntimeS = 1u << 19;
constexpr uint32_t kTelemetryFieldDistMilKm = 1u << 20;
constexpr uint32_t kTelemetryFieldDistSinceClearKm = 1u << 21;
constexpr uint32_t kTelemetryFieldAbsLoadPct = 1u << 22;
constexpr uint32_t kTelemetryFieldRelThrottlePct = 1u << 23;
constexpr uint32_t kTelemetryFieldLambda = 1u << 24;
constexpr uint32_t kTelemetryFieldCatalystB1S1C = 1u << 25;
constexpr uint32_t kTelemetryFieldFuelRailKpa = 1u << 26;
constexpr uint32_t kTelemetryFieldEgrCmdPct = 1u << 27;
constexpr uint32_t kTelemetryFieldEvapPurgePct = 1u << 28;
constexpr uint32_t kTelemetryFieldWarmupsSinceClear = 1u << 29;
constexpr uint32_t kTelemetryFieldDemandTorquePct = 1u << 30;
constexpr uint32_t kTelemetryFieldActualTorquePct = 1u << 31;

// Fields polled when start_stream names no metrics (the original four, so the
// update rate of existing apps does not drop).
constexpr uint32_t kTelemetryFieldDefault =
    kTelemetryFieldRpm | kTelemetryFieldCoolantC | kTelemetryFieldThrottlePct | kTelemetryFieldSpeedKmh;
constexpr uint32_t kTelemetryFieldAll = 0xFFFFFFFFu;

// Parses optional "metrics":["rpm",...] from command JSON. "all" selects every
// metric. Returns kTelemetryFieldDefault if absent or nothing matches.
uint32_t ParseTelemetryFieldMask(const String& raw_json);

}  // namespace core
}  // namespace obd
