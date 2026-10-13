#include "pid_decoder.h"

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
    {kTelemetryFieldEngineLoadPct, 0x04, "engine_load_pct"},
    {kTelemetryFieldIntakeAirC, 0x0F, "intake_air_c"},
    {kTelemetryFieldMafGs, 0x10, "maf_gs"},
    {kTelemetryFieldMapKpa, 0x0B, "map_kpa"},
    {kTelemetryFieldTimingAdvanceDeg, 0x0E, "timing_advance_deg"},
    {kTelemetryFieldThrottleValvePct, 0x11, "throttle_valve_pct"},
    {kTelemetryFieldFuelLevelPct, 0x2F, "fuel_level_pct"},
    {kTelemetryFieldBaroKpa, 0x33, "baro_kpa"},
    {kTelemetryFieldModuleVoltageV, 0x42, "module_voltage_v"},
    {kTelemetryFieldAmbientAirC, 0x46, "ambient_air_c"},
    {kTelemetryFieldOilTempC, 0x5C, "oil_temp_c"},
    {kTelemetryFieldFuelRateLh, 0x5E, "fuel_rate_lh"},
    {kTelemetryFieldStftB1Pct, 0x06, "stft_b1_pct"},
    {kTelemetryFieldLtftB1Pct, 0x07, "ltft_b1_pct"},
    {kTelemetryFieldFuelPressureKpa, 0x0A, "fuel_pressure_kpa"},
    {kTelemetryFieldRuntimeS, 0x1F, "runtime_s"},
    {kTelemetryFieldDistMilKm, 0x21, "dist_mil_km"},
    {kTelemetryFieldDistSinceClearKm, 0x31, "dist_since_clear_km"},
    {kTelemetryFieldAbsLoadPct, 0x43, "abs_load_pct"},
    {kTelemetryFieldRelThrottlePct, 0x45, "rel_throttle_pct"},
    {kTelemetryFieldLambda, 0x44, "lambda"},
    {kTelemetryFieldCatalystB1S1C, 0x3C, "catalyst_b1s1_c"},
    {kTelemetryFieldFuelRailKpa, 0x23, "fuel_rail_kpa"},
    {kTelemetryFieldEgrCmdPct, 0x2C, "egr_cmd_pct"},
    {kTelemetryFieldEvapPurgePct, 0x2E, "evap_purge_pct"},
    {kTelemetryFieldWarmupsSinceClear, 0x30, "warmups_since_clear"},
    {kTelemetryFieldDemandTorquePct, 0x61, "demand_torque_pct"},
    {kTelemetryFieldActualTorquePct, 0x62, "actual_torque_pct"},
};
const size_t kPidCatalogueLen = sizeof(kPidCatalogue) / sizeof(kPidCatalogue[0]);

namespace {

String Percent(uint8_t a) { return String(static_cast<int>((static_cast<uint32_t>(a) * 100u) / 255u)); }

String FuelTrim(uint8_t a) { return String(static_cast<float>(a) * 100.0f / 128.0f - 100.0f, 1); }

uint16_t Word(const uint8_t* payload) {
  return (static_cast<uint16_t>(payload[0]) << 8) | payload[1];
}

}  // namespace

String PidDecoder::FormatValue(uint8_t pid, const uint8_t* payload, uint8_t payload_len) const {
  if (payload == nullptr || payload_len == 0) {
    return String();
  }

  // PIDs whose value is two bytes (A, B).
  switch (pid) {
    case 0x0C:
    case 0x10:
    case 0x1F:
    case 0x21:
    case 0x23:
    case 0x31:
    case 0x3C:
    case 0x42:
    case 0x43:
    case 0x44:
    case 0x5E:
      if (payload_len < 2) {
        return String();
      }
      break;
    default:
      break;
  }

  const uint8_t a = payload[0];
  switch (pid) {
    case 0x04:  // Calculated engine load
    case 0x11:  // Throttle valve position
    case 0x2C:  // Commanded EGR
    case 0x2E:  // Commanded evaporative purge
    case 0x2F:  // Fuel tank level
    case 0x45:  // Relative throttle position
      return Percent(a);
    case 0x05: {  // Engine coolant temperature (deg C); 0xFF = not available
      if (a == 0xFF) {
        return String();
      }
      return String(static_cast<int>(a) - 40);
    }
    case 0x0F:  // Intake air temperature
    case 0x46:  // Ambient air temperature
    case 0x5C:  // Engine oil temperature
      return String(static_cast<int>(a) - 40);
    case 0x06:  // Short term fuel trim bank 1
    case 0x07:  // Long term fuel trim bank 1
      return FuelTrim(a);
    case 0x0A:  // Fuel pressure (gauge), 3 kPa per bit
      return String(static_cast<int>(a) * 3);
    case 0x0B:  // Intake manifold absolute pressure
    case 0x33:  // Barometric pressure
    case 0x30:  // Warm-ups since codes cleared
      return String(static_cast<int>(a));
    case 0x0C: {  // Engine RPM; 0xFFFF = not available
      const uint16_t raw = Word(payload);
      if (raw == 0xFFFF) {
        return String();
      }
      return String(raw / 4);
    }
    case 0x0D: {  // Vehicle speed (km/h); 0xFF = not available
      if (a == 0xFF) {
        return String();
      }
      return String(static_cast<int>(a));
    }
    case 0x0E:  // Timing advance (deg before TDC)
      return String(static_cast<float>(a) / 2.0f - 64.0f, 1);
    case 0x10:  // MAF air flow rate (g/s)
      return String(static_cast<float>(Word(payload)) / 100.0f, 2);
    case 0x1F:  // Run time since engine start (s)
    case 0x21:  // Distance travelled with MIL on (km)
    case 0x31:  // Distance travelled since codes cleared (km)
      return String(Word(payload));
    case 0x23:  // Fuel rail gauge pressure (kPa), 10 kPa per bit
      return String(static_cast<uint32_t>(Word(payload)) * 10u);
    case 0x3C:  // Catalyst temperature bank 1 sensor 1
      return String(static_cast<float>(Word(payload)) / 10.0f - 40.0f, 1);
    case 0x42:  // Control module voltage
      return String(static_cast<float>(Word(payload)) / 1000.0f, 2);
    case 0x43:  // Absolute load value
      return String(static_cast<int>((static_cast<uint32_t>(Word(payload)) * 100u) / 255u));
    case 0x44:  // Commanded air-fuel equivalence ratio (lambda)
      return String(static_cast<float>(Word(payload)) / 32768.0f, 3);
    case 0x49: {  // Accelerator pedal position D; 0xFF = not available
      if (a == 0xFF) {
        return String();
      }
      return Percent(a);
    }
    case 0x5E:  // Engine fuel rate (L/h)
      return String(static_cast<float>(Word(payload)) / 20.0f, 2);
    case 0x61:  // Driver's demand engine percent torque
    case 0x62:  // Actual engine percent torque
      return String(static_cast<int>(a) - 125);
    default:
      return String();
  }
}

}  // namespace core
}  // namespace obd
