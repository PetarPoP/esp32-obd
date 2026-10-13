#include "pid_decoder.h"

#include <math.h>

namespace obd {
namespace core {

namespace {

constexpr float kPct = 100.0f / 255.0f;   // A * 100 / 255
constexpr float kTrim = 100.0f / 128.0f;  // A * 100 / 128 - 100
constexpr float kLambda = 2.0f / 65536.0f;

}  // namespace

// Linear value: raw * scale + bias.
#define PF(pid, key, off, size, scale, bias, dec) {pid, key, off, size, 0, 0, scale, bias, dec, 0}
// Linear value where an all-ones raw value means "not available".
#define PFN(pid, key, off, size, scale, bias, dec) \
  {pid, key, off, size, 0, 0, scale, bias, dec, kFieldNaAllOnes}
// Signed (two's complement) linear value.
#define PFS(pid, key, off, size, scale, bias, dec) \
  {pid, key, off, size, 0, 0, scale, bias, dec, kFieldSigned}
// Bit field / raw integer: (raw >> shift) & mask.
#define PFB(pid, key, off, size, shift, mask) {pid, key, off, size, shift, mask, 1.0f, 0.0f, 0, 0}

// Oxygen sensors 1..8 (bank 1 sensors 1-4, then bank 2), three PID groups.
#define O2_NARROW(n)                                                    \
  PF(0x13 + n, "o2_s" #n "_v", 0, 1, 0.005f, 0.0f, 3),                   \
      PFN(0x13 + n, "o2_s" #n "_trim_pct", 1, 1, kTrim, -100.0f, 1)
#define O2_WIDE_V(n)                                                    \
  PF(0x23 + n, "o2_s" #n "_lambda", 0, 2, kLambda, 0.0f, 3),             \
      PF(0x23 + n, "o2_s" #n "_wide_v", 2, 2, 8.0f / 65536.0f, 0.0f, 3)
#define O2_WIDE_I(n)                                                    \
  PF(0x33 + n, "o2_s" #n "_lambda_c", 0, 2, kLambda, 0.0f, 3),           \
      PF(0x33 + n, "o2_s" #n "_current_ma", 2, 2, 1.0f / 256.0f, -128.0f, 2)

const PidField kPidCatalogue[] = {
    PFB(0x01, "mil_on", 0, 1, 7, 0x01),
    PFB(0x01, "dtc_count", 0, 1, 0, 0x7F),
    PFB(0x03, "fuel_system_status", 0, 1, 0, 0),
    PF(0x04, "engine_load_pct", 0, 1, kPct, 0.0f, 1),
    PFN(0x05, "coolant_c", 0, 1, 1.0f, -40.0f, 0),
    PF(0x06, "stft_b1_pct", 0, 1, kTrim, -100.0f, 1),
    PF(0x07, "ltft_b1_pct", 0, 1, kTrim, -100.0f, 1),
    PF(0x08, "stft_b2_pct", 0, 1, kTrim, -100.0f, 1),
    PF(0x09, "ltft_b2_pct", 0, 1, kTrim, -100.0f, 1),
    PF(0x0A, "fuel_pressure_kpa", 0, 1, 3.0f, 0.0f, 0),
    PF(0x0B, "map_kpa", 0, 1, 1.0f, 0.0f, 0),
    PFN(0x0C, "rpm", 0, 2, 0.25f, 0.0f, 0),
    PFN(0x0D, "speed_kmh", 0, 1, 1.0f, 0.0f, 0),
    PF(0x0E, "timing_advance_deg", 0, 1, 0.5f, -64.0f, 1),
    PF(0x0F, "intake_air_c", 0, 1, 1.0f, -40.0f, 0),
    PF(0x10, "maf_gs", 0, 2, 0.01f, 0.0f, 2),
    PF(0x11, "throttle_valve_pct", 0, 1, kPct, 0.0f, 1),
    PFB(0x12, "sec_air_status", 0, 1, 0, 0),
    PFB(0x13, "o2_sensors_present", 0, 1, 0, 0),
    O2_NARROW(1), O2_NARROW(2), O2_NARROW(3), O2_NARROW(4),
    O2_NARROW(5), O2_NARROW(6), O2_NARROW(7), O2_NARROW(8),
    PFB(0x1C, "obd_standard", 0, 1, 0, 0),
    PFB(0x1E, "pto_active", 0, 1, 0, 0x01),
    PF(0x1F, "runtime_s", 0, 2, 1.0f, 0.0f, 0),
    PF(0x21, "dist_mil_km", 0, 2, 1.0f, 0.0f, 0),
    PF(0x22, "fuel_rail_rel_kpa", 0, 2, 0.079f, 0.0f, 1),
    PF(0x23, "fuel_rail_kpa", 0, 2, 10.0f, 0.0f, 0),
    O2_WIDE_V(1), O2_WIDE_V(2), O2_WIDE_V(3), O2_WIDE_V(4),
    O2_WIDE_V(5), O2_WIDE_V(6), O2_WIDE_V(7), O2_WIDE_V(8),
    PF(0x2C, "egr_cmd_pct", 0, 1, kPct, 0.0f, 1),
    PF(0x2D, "egr_error_pct", 0, 1, kTrim, -100.0f, 1),
    PF(0x2E, "evap_purge_pct", 0, 1, kPct, 0.0f, 1),
    PF(0x2F, "fuel_level_pct", 0, 1, kPct, 0.0f, 1),
    PF(0x30, "warmups_since_clear", 0, 1, 1.0f, 0.0f, 0),
    PF(0x31, "dist_since_clear_km", 0, 2, 1.0f, 0.0f, 0),
    PFS(0x32, "evap_vapor_pa", 0, 2, 0.25f, 0.0f, 1),
    PF(0x33, "baro_kpa", 0, 1, 1.0f, 0.0f, 0),
    O2_WIDE_I(1), O2_WIDE_I(2), O2_WIDE_I(3), O2_WIDE_I(4),
    O2_WIDE_I(5), O2_WIDE_I(6), O2_WIDE_I(7), O2_WIDE_I(8),
    PF(0x3C, "catalyst_b1s1_c", 0, 2, 0.1f, -40.0f, 1),
    PF(0x3D, "catalyst_b2s1_c", 0, 2, 0.1f, -40.0f, 1),
    PF(0x3E, "catalyst_b1s2_c", 0, 2, 0.1f, -40.0f, 1),
    PF(0x3F, "catalyst_b2s2_c", 0, 2, 0.1f, -40.0f, 1),
    PF(0x42, "module_voltage_v", 0, 2, 0.001f, 0.0f, 2),
    PF(0x43, "abs_load_pct", 0, 2, kPct, 0.0f, 1),
    PF(0x44, "lambda", 0, 2, kLambda, 0.0f, 3),
    PF(0x45, "rel_throttle_pct", 0, 1, kPct, 0.0f, 1),
    PF(0x46, "ambient_air_c", 0, 1, 1.0f, -40.0f, 0),
    PF(0x47, "abs_throttle_b_pct", 0, 1, kPct, 0.0f, 1),
    PF(0x48, "abs_throttle_c_pct", 0, 1, kPct, 0.0f, 1),
    // PID 0x49 (Accelerator pedal position D) tracks the gas pedal directly on
    // drive-by-wire cars; PID 0x11 (Throttle valve position) is what the ECU
    // commands to the valve and on many ECUs sits ~constant at idle / cruise.
    PFN(0x49, "throttle_pct", 0, 1, kPct, 0.0f, 0),
    PF(0x4A, "pedal_e_pct", 0, 1, kPct, 0.0f, 1),
    PF(0x4B, "pedal_f_pct", 0, 1, kPct, 0.0f, 1),
    PF(0x4C, "cmd_throttle_pct", 0, 1, kPct, 0.0f, 1),
    PF(0x4D, "time_mil_min", 0, 2, 1.0f, 0.0f, 0),
    PF(0x4E, "time_since_clear_min", 0, 2, 1.0f, 0.0f, 0),
    PF(0x50, "maf_max_gs", 0, 1, 10.0f, 0.0f, 0),
    PFB(0x51, "fuel_type", 0, 1, 0, 0),
    PF(0x52, "ethanol_pct", 0, 1, kPct, 0.0f, 1),
    PF(0x53, "evap_abs_kpa", 0, 2, 0.005f, 0.0f, 3),
    PFS(0x54, "evap_vapor2_pa", 0, 2, 1.0f, 0.0f, 0),
    PF(0x55, "st_o2_trim_b1_pct", 0, 1, kTrim, -100.0f, 1),
    PF(0x56, "lt_o2_trim_b1_pct", 0, 1, kTrim, -100.0f, 1),
    PF(0x57, "st_o2_trim_b2_pct", 0, 1, kTrim, -100.0f, 1),
    PF(0x58, "lt_o2_trim_b2_pct", 0, 1, kTrim, -100.0f, 1),
    PF(0x59, "fuel_rail_abs_kpa", 0, 2, 10.0f, 0.0f, 0),
    PF(0x5A, "rel_pedal_pct", 0, 1, kPct, 0.0f, 1),
    PF(0x5B, "hybrid_battery_pct", 0, 1, kPct, 0.0f, 1),
    PF(0x5C, "oil_temp_c", 0, 1, 1.0f, -40.0f, 0),
    PF(0x5D, "injection_timing_deg", 0, 2, 1.0f / 128.0f, -210.0f, 2),
    PF(0x5E, "fuel_rate_lh", 0, 2, 0.05f, 0.0f, 2),
    PF(0x61, "demand_torque_pct", 0, 1, 1.0f, -125.0f, 0),
    PF(0x62, "actual_torque_pct", 0, 1, 1.0f, -125.0f, 0),
    PF(0x63, "ref_torque_nm", 0, 2, 1.0f, 0.0f, 0),
    PF(0x64, "torque_idle_pct", 0, 1, 1.0f, -125.0f, 0),
    // From 0x66 on most PIDs start with a "sensors present" byte (A).
    PF(0x66, "maf_a_gs", 1, 2, 1.0f / 32.0f, 0.0f, 2),
    PF(0x67, "coolant_1_c", 1, 1, 1.0f, -40.0f, 0),
    PF(0x67, "coolant_2_c", 2, 1, 1.0f, -40.0f, 0),
    PF(0x68, "intake_air_1_c", 1, 1, 1.0f, -40.0f, 0),
    PF(0x70, "boost_cmd_kpa", 1, 2, 1.0f / 32.0f, 0.0f, 1),
    PF(0x70, "boost_kpa", 3, 2, 1.0f / 32.0f, 0.0f, 1),
    PF(0x73, "exhaust_pressure_kpa", 1, 2, 0.01f, 0.0f, 2),
    PF(0x74, "turbo_rpm", 1, 2, 10.0f, 0.0f, 0),
    PF(0x77, "charge_air_c", 1, 1, 1.0f, -40.0f, 0),
    PF(0x78, "egt_b1s1_c", 1, 2, 0.1f, -40.0f, 1),
    PF(0x79, "egt_b2s1_c", 1, 2, 0.1f, -40.0f, 1),
    PF(0x7A, "dpf_delta_kpa", 1, 2, 0.01f, 0.0f, 2),
    PF(0x7F, "engine_run_total_s", 1, 4, 1.0f, 0.0f, 0),
    PF(0x83, "nox_ppm", 1, 2, 1.0f, 0.0f, 0),
    PF(0x84, "manifold_surface_c", 0, 1, 1.0f, -40.0f, 0),
    PF(0x8E, "friction_torque_pct", 0, 1, 1.0f, -125.0f, 0),
    PF(0x9D, "fuel_rate_gs", 0, 2, 0.02f, 0.0f, 2),
    PF(0x9E, "exhaust_flow_kgh", 0, 2, 0.2f, 0.0f, 1),
    PF(0xA2, "cyl_fuel_rate_mg", 0, 2, 1.0f / 32.0f, 0.0f, 2),
    PFB(0xA4, "trans_gear", 1, 1, 4, 0x0F),
    PF(0xA4, "trans_ratio", 2, 2, 0.001f, 0.0f, 3),
    PF(0xA6, "odometer_km", 0, 4, 0.1f, 0.0f, 1),
};
const size_t kPidCatalogueLen = sizeof(kPidCatalogue) / sizeof(kPidCatalogue[0]);

#undef PF
#undef PFN
#undef PFS
#undef PFB
#undef O2_NARROW
#undef O2_WIDE_V
#undef O2_WIDE_I

uint8_t PidDataLength(uint8_t pid) {
  // SAE J1979 data lengths for PIDs 0x00..0x63 (0 = not a Mode 01 PID).
  static const uint8_t kLengths[0x64] = {
      4, 4, 0, 2, 1, 1, 1, 1, 1, 1, 1, 1, 2, 1, 1, 1,  // 0x00
      2, 1, 1, 1, 2, 2, 2, 2, 2, 2, 2, 2, 1, 1, 1, 2,  // 0x10
      4, 2, 2, 2, 4, 4, 4, 4, 4, 4, 4, 4, 1, 1, 1, 1,  // 0x20
      1, 2, 2, 1, 4, 4, 4, 4, 4, 4, 4, 4, 2, 2, 2, 2,  // 0x30
      4, 4, 2, 2, 2, 1, 1, 1, 1, 1, 1, 1, 1, 2, 2, 4,  // 0x40
      4, 1, 1, 2, 2, 2, 2, 2, 2, 2, 1, 1, 1, 2, 2, 1,  // 0x50
      4, 1, 1, 2,                                      // 0x60
  };
  return pid < sizeof(kLengths) ? kLengths[pid] : 0;
}

String PidDecoder::FormatField(const PidField& field, const uint8_t* payload,
                               uint8_t payload_len) const {
  if (payload == nullptr || field.size == 0 || field.size > 4 ||
      static_cast<uint16_t>(field.offset) + field.size > payload_len) {
    return String();
  }
  uint32_t raw = 0;
  for (uint8_t i = 0; i < field.size; ++i) {
    raw = (raw << 8) | payload[field.offset + i];
  }
  if ((field.flags & kFieldNaAllOnes) != 0) {
    const uint32_t all_ones = field.size == 4 ? 0xFFFFFFFFu : ((1u << (8 * field.size)) - 1u);
    if (raw == all_ones) {
      return String();
    }
  }
  if (field.mask != 0 || field.shift != 0) {
    raw >>= field.shift;
    if (field.mask != 0) {
      raw &= field.mask;
    }
    return String(raw);
  }

  double value;
  if ((field.flags & kFieldSigned) != 0) {
    const uint8_t bits = 8 * field.size;
    int32_t signed_raw = static_cast<int32_t>(raw);
    if (bits < 32 && (raw & (1u << (bits - 1))) != 0) {
      signed_raw -= static_cast<int32_t>(1u << bits);
    }
    value = static_cast<double>(signed_raw);
  } else {
    value = static_cast<double>(raw);
  }
  value = value * field.scale + field.bias;
  if (field.decimals == 0) {
    return String(static_cast<long>(lround(value)));
  }
  return String(value, static_cast<unsigned int>(field.decimals));
}

}  // namespace core
}  // namespace obd
