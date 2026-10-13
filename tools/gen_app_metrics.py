#!/usr/bin/env python3
"""Generates mobile/src/data/metrics.generated.ts from the firmware sources.

Keeps the app's metric catalogue (keys, units, PIDs) in sync with
esp32-obd/pid_decoder.cpp (standard Mode 01) and smart_bms_service.cpp
(Smart ED). Run after changing either file:

    python tools/gen_app_metrics.py
"""

import json
import os
import re

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
FW = os.path.join(ROOT, "esp32-obd")
OUT = os.path.join(ROOT, "mobile", "src", "data", "metrics.generated.ts")

# Unit from the key suffix (longest match first).
UNITS = [
    ("_kwh100", "kWh/100km"), ("_kmh", "km/h"), ("_kgh", "kg/h"), ("_kohm", "kΩ"), ("_mbar", "mbar"),
    ("_kpa", "kPa"), ("_pct", "%"), ("_deg", "°"), ("_rpm", "rpm"), ("_ppm", "ppm"), ("_nm", "Nm"),
    ("_lh", "L/h"), ("_gs", "g/s"), ("_mg", "mg"), ("_mv", "mV"), ("_ma", "mA"), ("_ah", "Ah"),
    ("_kw", "kW"), ("_km", "km"), ("_min", "min"), ("_amps", "A"), ("_pa", "Pa"), ("_c", "°C"),
    ("_v", "V"), ("_a", "A"), ("_s", "s"), ("_h", "h"),
]

LABELS = {
    "rpm": "Engine RPM",  # unit set in unit_for "speed_kmh": "Vehicle speed", "coolant_c": "Coolant temp",
    "throttle_pct": "Accelerator pedal", "engine_load_pct": "Engine load", "abs_load_pct": "Absolute load",
    "intake_air_c": "Intake air temp", "maf_gs": "MAF air flow", "map_kpa": "Manifold pressure",
    "timing_advance_deg": "Timing advance", "throttle_valve_pct": "Throttle valve", "fuel_level_pct": "Fuel level",
    "baro_kpa": "Barometric pressure", "module_voltage_v": "Battery / module voltage",
    "ambient_air_c": "Ambient air temp", "oil_temp_c": "Oil temp", "fuel_rate_lh": "Fuel rate",
    "stft_b1_pct": "Short fuel trim B1", "ltft_b1_pct": "Long fuel trim B1", "stft_b2_pct": "Short fuel trim B2",
    "ltft_b2_pct": "Long fuel trim B2", "fuel_pressure_kpa": "Fuel pressure", "runtime_s": "Run time",
    "dist_mil_km": "Distance with MIL on", "dist_since_clear_km": "Distance since DTC clear",
    "rel_throttle_pct": "Relative throttle", "lambda": "Commanded lambda", "fuel_rail_kpa": "Fuel rail pressure",
    "fuel_rail_rel_kpa": "Fuel rail pressure (rel.)", "fuel_rail_abs_kpa": "Fuel rail pressure (abs.)",
    "egr_cmd_pct": "Commanded EGR", "egr_error_pct": "EGR error", "evap_purge_pct": "EVAP purge",
    "warmups_since_clear": "Warm-ups since clear", "demand_torque_pct": "Demanded torque",
    "actual_torque_pct": "Actual torque", "ref_torque_nm": "Reference torque", "odometer_km": "Odometer",
    "mil_on": "MIL (check engine)", "dtc_count": "DTC count", "fuel_system_status": "Fuel system status",
    "sec_air_status": "Secondary air status", "o2_sensors_present": "O2 sensors present",
    "obd_standard": "OBD standard", "pto_active": "PTO active", "fuel_type": "Fuel type",
    "ethanol_pct": "Ethanol", "hybrid_battery_pct": "Hybrid battery", "trans_gear": "Gear",
    "trans_ratio": "Gear ratio", "boost_kpa": "Boost pressure", "boost_cmd_kpa": "Commanded boost",
    "turbo_rpm": "Turbo speed", "egt_b1s1_c": "Exhaust temp B1S1", "egt_b2s1_c": "Exhaust temp B2S1",
    "dpf_delta_kpa": "DPF differential pressure", "nox_ppm": "NOx", "charge_air_c": "Charge air temp",
    "time_mil_min": "Time with MIL on", "time_since_clear_min": "Time since DTC clear",
    "engine_run_total_s": "Total engine run time",
    # Smart ED
    "soc_pct": "SOC (display)", "soc_internal_pct": "SOC (internal)", "hv_v": "HV battery",
    "hv_amps": "HV current", "power_kw": "Power", "lv_v": "12 V battery", "odo_km": "Odometer",
    "time_hh": "Clock hours", "time_mm": "Clock minutes", "range_km": "Range",
    "usable_power_pct": "Usable power", "energy_start_kwh100": "Consumption since start",
    "energy_reset_kwh100": "Consumption since reset", "hv_active": "HV active",
    "soh_pct": "SOH (estimate)", "batt_status": "Battery status flags", "bms_amps": "BMS current",
    "bms_hv_v": "BMS HV voltage", "isolation_kohm": "DC isolation", "dc_fault": "DC fault flag",
    "cap_init_ah": "Initial capacity", "cap_loss_pct": "Capacity loss",
    "contactor_cycles_left": "Contactor cycles left", "contactor_cycles_max": "Contactor cycles max",
    "contactor_state": "Contactor state", "coolant_in_c": "Battery coolant inlet",
    "cell_count": "Cells read", "cell_min_no": "Lowest cell #", "cell_max_no": "Highest cell #",
    "charger_nlg6": "NLG6 fast charger", "cable_max_a": "Cable max current",
    "chargepoint_max_a": "Charge point max current", "intake_air_1_c": "Intake air temp 1",
    "coolant_1_c": "Coolant temp 1", "coolant_2_c": "Coolant temp 2", "rel_pedal_pct": "Relative pedal",
    "evap_vapor_pa": "EVAP vapor pressure", "evap_vapor2_pa": "EVAP vapor pressure 2",
    "evap_abs_kpa": "EVAP absolute pressure", "maf_a_gs": "MAF sensor A", "maf_max_gs": "MAF max",
    "fuel_rate_gs": "Fuel rate (mass)", "cyl_fuel_rate_mg": "Fuel per stroke", "exhaust_flow_kgh": "Exhaust flow",
    "exhaust_pressure_kpa": "Exhaust pressure", "injection_timing_deg": "Injection timing",
    "friction_torque_pct": "Friction torque", "torque_idle_pct": "Torque at idle",
    "manifold_surface_c": "Manifold surface temp", "cmd_throttle_pct": "Commanded throttle",
    "hv_off_time_s": "HV off time", "hv_lowcurrent_s": "HV low-current time", "ocv_timer_s": "OCV timer",
    "last_meas_days": "Days since capacity test", "cap_meas_quality": "Capacity test quality",
    "cap_combined_quality": "Capacity estimate quality", "cooling_temp_c": "Cooling temp",
    "pump_temp_c": "Cooling pump temp", "pump_lv_v": "Cooling pump voltage", "pump_amps": "Cooling pump current",
    "pump_pct": "Cooling pump speed", "pump_otr_h": "Cooling pump hours", "fan_pct": "Cooling fan speed",
    "fan_otr_h": "Cooling fan hours", "heater_otr_h": "Battery heater hours", "heater_on": "Battery heater on",
    "vacuum_otr_h": "Vacuum pump hours", "charger_plate_c": "Charger plate temp", "charger_temp_c": "Charger temp",
    "charger_socket_c": "Charging socket temp", "charger_set_amps": "Charge current setpoint",
    "charger_lv_v": "Charger 12 V", "charger_dc_v": "Charger DC voltage", "charger_dc_amps": "Charger DC current",
}

WORDS = {
    "b1": "B1", "b2": "B2", "s1": "S1", "s2": "S2", "o2": "O2", "dc": "DC", "ac": "AC", "hv": "HV",
    "lv": "LV", "cmd": "cmd.", "adc": "ADC", "obl": "OBL", "otr": "run time", "eco": "ECO", "mv": "",
}


PATTERNS = [
    (r"cell_mv_(min|mean|max|stddev)$", "Cell voltage {0}", "mV"),
    (r"adc_cell_mv_(min|mean|max)$", "Cell voltage {0} (BMS ADC)", "mV"),
    (r"cap_ah_(min|mean|max)$", "Capacity {0} (BMS)", "Ah"),
    (r"cell_cap_ah_(min|mean|max)$", "Cell capacity {0}", "Ah"),
    (r"temp_(min|mean|max)_c$", "Battery temp {0}", "°C"),
    (r"batt_t(\d)_c$", "Battery sensor {0}", "°C"),
    (r"ac_l(\d)_v$", "AC L{0} voltage", "V"),
    (r"ac_l(\d)_a$", "AC L{0} current", "A"),
    (r"catalyst_b(\d)s(\d)_c$", "Catalyst temp B{0}S{1}", "°C"),
    (r"abs_throttle_([bc])_pct$", "Absolute throttle {0}", "%"),
    (r"pedal_([ef])_pct$", "Accelerator pedal {0}", "%"),
    (r"(st|lt)_o2_trim_b(\d)_pct$", "{0} O2 trim B{1}", "%"),
    (r"eco_(accel|const|coast|total)_pct$", "ECO {0}", "%"),
    (r"trip_(start|reset)_km$", "Trip since {0}", "km"),
    (r"vacuum_p(\d)_mbar$", "Vacuum pressure {0}", "mbar"),
]
PATTERN_WORDS = {"min": "min", "mean": "mean", "max": "max", "stddev": "σ", "st": "Short", "lt": "Long",
                 "accel": "acceleration", "const": "constant", "coast": "coasting", "total": "total",
                 "b": "B", "c": "C", "e": "E", "f": "F", "start": "start", "reset": "reset"}


def pattern_match(key):
    for pattern, label, unit in PATTERNS:
        m = re.match(pattern, key)
        if m:
            parts = [PATTERN_WORDS.get(g, g) for g in m.groups()]
            return label.format(*parts), unit
    return None


def unit_for(key):
    if key in ("rpm", "turbo_rpm"):
        return "rpm"
    matched = pattern_match(key)
    if matched:
        return matched[1]
    for suffix, unit in UNITS:
        if key.endswith(suffix):
            return unit
    return ""


def label_for(key):
    if key in LABELS:
        return LABELS[key]
    matched = pattern_match(key)
    if matched:
        return matched[0]
    m = re.match(r"o2_s(\d)_(.*)", key)
    if m:
        rest = {"v": "voltage", "trim_pct": "trim", "lambda": "lambda", "wide_v": "voltage (wide)",
                "lambda_c": "lambda (current)", "current_ma": "current"}.get(m.group(2), m.group(2))
        return f"O2 sensor {m.group(1)} {rest}"
    words = key.split("_")
    for suffix, _ in UNITS:
        if key.endswith(suffix) and len(words) > 1:
            words = words[:-1]
            break
    out = [WORDS.get(w, w) for w in words]
    text = " ".join(w for w in out if w)
    return text[:1].upper() + text[1:]


def obd_fields():
    src = open(os.path.join(FW, "pid_decoder.cpp"), encoding="utf-8").read()
    body = src.split("const PidField kPidCatalogue[] = {")[1].split("};")[0]
    fields = []
    for m in re.finditer(r"(PFN|PFS|PFB|PF|O2_NARROW|O2_WIDE_V|O2_WIDE_I)\(([^()]*)\)", body):
        kind, args = m.group(1), [a.strip() for a in m.group(2).split(",")]
        if kind.startswith("O2_"):
            n = int(args[0])
            base, suffixes = {
                "O2_NARROW": (0x13, ["v", "trim_pct"]),
                "O2_WIDE_V": (0x23, ["lambda", "wide_v"]),
                "O2_WIDE_I": (0x33, ["lambda_c", "current_ma"]),
            }[kind]
            for s in suffixes:
                fields.append((f"o2_s{n}_{s}", base + n))
        else:
            fields.append((args[1].strip('"'), int(args[0], 16)))
    return fields


def smart_keys():
    src = open(os.path.join(FW, "smart_bms_service.cpp"), encoding="utf-8").read()
    body = src.split("const MetricInfo kMetrics[kMetricCount] = {")[1].split("};")[0]
    return re.findall(r'\{"([a-z0-9_]+)",\s*\d+\}', body)


def main():
    obd = [{"id": k, "label": label_for(k), "unit": unit_for(k) if k != "lambda" else "", "pid": f"0x{p:02X}"}
           for k, p in obd_fields()]
    smart = [{"id": k, "label": label_for(k), "unit": unit_for(k), "pid": "Smart ED"} for k in smart_keys()]
    os.makedirs(os.path.dirname(OUT), exist_ok=True)
    with open(OUT, "w", encoding="utf-8", newline="\n") as f:
        f.write("// Generated by tools/gen_app_metrics.py from the firmware sources. Do not edit.\n\n")
        f.write("export type MetricDefinition = { id: string; label: string; unit: string; pid: string };\n\n")
        f.write("export const OBD_METRICS: readonly MetricDefinition[] = ")
        f.write(json.dumps(obd, ensure_ascii=False, indent=2))
        f.write(";\n\nexport const SMART_ED_METRICS: readonly MetricDefinition[] = ")
        f.write(json.dumps(smart, ensure_ascii=False, indent=2))
        f.write(";\n")
    print(f"{len(obd)} OBD metrics, {len(smart)} Smart ED metrics -> {os.path.relpath(OUT, ROOT)}")


if __name__ == "__main__":
    main()
