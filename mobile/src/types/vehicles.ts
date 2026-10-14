import { OBD_METRICS, SMART_ED_METRICS, type MetricDefinition } from "../data/metrics.generated";
import type { ProfileCommand, ProfileSignal } from "../services/profileRepository";
import type { LinkConfig } from "./protocol";

/** The car the user picked. A make & model also gets manufacturer signals. */
export type VehicleSelection =
  | { kind: "generic" }
  | { kind: "smart_ed3" }
  | { kind: "profile"; make: string; model: string; file: string };

export type VehicleKind = VehicleSelection["kind"];

/** A manufacturer signal as stored on the phone (command without its other signals). */
export type StoredExtSignal = {
  key: string;
  name: string;
  unit: string;
  path: string;
  command: Omit<ProfileCommand, "signals">;
  signal: ProfileSignal;
};

/** What the user chose to read for one vehicle. */
export type VehicleValues = {
  /** Standard OBD-II (or Smart ED) metric keys. */
  metrics: string[];
  /** Manufacturer signals (make & model only). */
  ext: StoredExtSignal[];
};

export function vehicleKey(v: VehicleSelection): string {
  return v.kind === "profile" ? `profile:${v.file}` : v.kind;
}

export function vehicleName(v: VehicleSelection): string {
  switch (v.kind) {
    case "generic":
      return "Generic OBD-II";
    case "smart_ed3":
      return "Smart ED (451)";
    default:
      return `${v.make} ${v.model}`;
  }
}

export function linkConfig(v: VehicleSelection): LinkConfig {
  return v.kind === "smart_ed3"
    ? { baudKbps: 500, mode: "active", streamMode: "telemetry", diagnosticProfile: "smart_ed3" }
    : { baudKbps: 500, mode: "active", streamMode: "telemetry", diagnosticProfile: "obd_mode01", protocol: "auto" };
}

/** Standard metric catalogue for the vehicle kind. */
export function catalogueFor(v: VehicleSelection): readonly MetricDefinition[] {
  return v.kind === "smart_ed3" ? SMART_ED_METRICS : OBD_METRICS;
}

export function defaultValues(v: VehicleSelection): VehicleValues {
  if (v.kind === "smart_ed3") {
    return {
      metrics: ["soc_pct", "hv_v", "hv_amps", "power_kw", "lv_v", "range_km", "soh_pct", "temp_max_c", "speed_kmh"],
      ext: [],
    };
  }
  return {
    metrics: ["rpm", "speed_kmh", "coolant_c", "throttle_pct", "engine_load_pct", "module_voltage_v"],
    ext: [],
  };
}
