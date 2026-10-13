import { OBD_METRICS, SMART_ED_METRICS, type MetricDefinition } from "../data/metrics.generated";
import type { LinkConfig } from "./protocol";

export type VehicleKind = "generic" | "smart_ed3" | "profile";

export type VehicleMode = {
  id: VehicleKind;
  label: string;
  notes: string;
  link: LinkConfig;
  metrics: readonly MetricDefinition[];
  defaultMetrics: readonly string[];
};

export const VEHICLE_MODES: readonly VehicleMode[] = [
  {
    id: "generic",
    label: "Generic OBD-II (auto-detect)",
    notes:
      "Any car with OBD-II over CAN (EU/US from 2008). The ESP detects 11/29-bit CAN at 500/250 kbps, " +
      "reads the supported PIDs and polls up to six per request.",
    link: { baudKbps: 500, mode: "active", streamMode: "telemetry", diagnosticProfile: "obd_mode01", protocol: "auto" },
    metrics: OBD_METRICS,
    defaultMetrics: ["rpm", "speed_kmh", "coolant_c", "throttle_pct"],
  },
  {
    id: "profile",
    label: "Make & model (manufacturer PIDs)",
    notes:
      "Standard OBD-II plus manufacturer-specific values for the chosen make and model (OBDb profiles, " +
      "community data). Pick the signals below and load them to the ESP.",
    link: { baudKbps: 500, mode: "active", streamMode: "telemetry", diagnosticProfile: "obd_mode01", protocol: "auto" },
    metrics: OBD_METRICS,
    defaultMetrics: ["rpm", "speed_kmh", "coolant_c", "throttle_pct"],
  },
  {
    id: "smart_ed3",
    label: "Smart ED (451, 3rd gen)",
    notes:
      "Smart fortwo electric drive 2012–2015: broadcast frames plus UDS queries to BMS, cooling and charger " +
      "(ported from ED_BMSdiag). The ESP streams every available value; pick which ones to show.",
    link: { baudKbps: 500, mode: "active", streamMode: "telemetry", diagnosticProfile: "smart_ed3" },
    metrics: SMART_ED_METRICS,
    defaultMetrics: [
      "soc_pct", "hv_v", "hv_amps", "power_kw", "lv_v", "range_km", "soh_pct", "cap_ah_mean",
      "temp_max_c", "temp_min_c", "cell_mv_min", "cell_mv_max", "speed_kmh", "odo_km",
    ],
  },
];
